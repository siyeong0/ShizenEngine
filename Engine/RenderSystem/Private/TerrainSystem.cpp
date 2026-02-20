#include "pch.h"
#include "Engine/RenderSystem/Public/TerrainSystem.h"

#include <cmath>
#include <cfloat>
#include <cstring>

#include "Engine/AssetManager/Public/AssetManager.h"
#include "Engine/RuntimeData/Public/MaterialManager.h"

#include "Engine/Renderer/Public/Renderer.h"
#include "Engine/Renderer/Public/RenderPassBuilder.h"
#include "Engine/Renderer/Public/RenderPassContext.h"
#include "Engine/Renderer/Public/RenderResourceRegistry.h"

#include "Engine/GraphicsTools/Public/MapHelper.hpp"

namespace shz
{
	namespace hlsl
	{
#include "Shaders/HLSL_Structures.hlsli"
	} // namespace hlsl

	struct TerrainVertex final
	{
		float3 Pos;     // x,z in [0..ChunkGridRes]
		float2 UV;      // [0..1]
		float3 Normal;  // unused (VS computes)
		float3 Tangent; // unused
	};

	enum ETerrainStitchMask : uint8
	{
		STITCH_NONE = 0,
		STITCH_LEFT = 1 << 0,
		STITCH_RIGHT = 1 << 1,
		STITCH_BOTTOM = 1 << 2,
		STITCH_TOP = 1 << 3,
	};

	// Helpers
	uint32 log2U32(uint32 v) noexcept
	{
		uint32 r = 0;
		while (v > 1u)
		{
			v >>= 1u;
			++r;
		}
		return r;
	}

	static inline float u16ToNormalized(uint16 v) noexcept
	{
		return static_cast<float>(v) * (1.0f / 65535.0f);
	}

	static inline uint16 normalizedToU16(float n) noexcept
	{
		const float c = Clamp01(n);
		const float scaled = c * 65535.0f;
		const uint32 iv = static_cast<uint32>(scaled + 0.5f);
		return static_cast<uint16>((iv > 65535u) ? 65535u : iv);
	}

	// Lifecycle
	void TerrainSystem::Initialize(Renderer& renderer, AssetManager& assetManager, const CreateInfo& ci)
	{
		Cleanup();
		m_CI = ci;

		ASSERT(!m_CI.HeightPath.empty(), "TerrainSystem HeightPath is empty.");
		ASSERT(m_CI.ChunkSize > EPSILON, "ChunkSizeMeters must be > 0.");
		ASSERT(m_CI.CellSize > EPSILON, "CellSizeMeters must be > 0.");
		ASSERT(m_CI.WorldSpacing > EPSILON, "WorldSpacingMeters must be > 0.");
		ASSERT(m_CI.HeightScale >= EPSILON, "HeightScale must be >= 0.");

		m_ChunkSize = m_CI.ChunkSize;
		m_CellSize = m_CI.CellSize;
		m_WorldSpacing = m_CI.WorldSpacing;

		m_HeightScale = m_CI.HeightScale;
		m_HeightOffset = m_CI.HeightOffset;
		m_bCenterXZ = m_CI.bCenterXZ;

		// Derive grid resolution from ChunkSize / CellSize.
		{
			const float resolution = m_ChunkSize / m_CellSize;
			const uint32 res = uint32(std::lround(resolution));

			ASSERT(std::fabs(resolution - float(res)) < 1e-3f, "ChunkSizeMeters/CellSizeMeters must be an integer. (ChunkSize=%.3f, CellSize=%.3f, resF=%.3f)", m_ChunkSize, m_CellSize, resolution);

			ASSERT(res >= 2u, "Derived ChunkGridRes too small.");
			ASSERT((res != 0u) && ((res & (res - 1u)) == 0u), "Derived ChunkGridRes must be power-of-two. (res=%u)", res);

			// Enforce uint16 indexability: (resolution+1)^2 <= 65535.
			const uint32 vertsPerSide = res + 1u;
			ASSERT(vertsPerSide * vertsPerSide <= 65535u, "ChunkGridRes too large for uint16 indices. res=%u => vertsPerSide=%u => verts=%u", res, vertsPerSide, vertsPerSide * vertsPerSide);

			m_ChunkGridRes = res;
		}

		// LOD count: log2(res) + 1.
		m_NumLods = log2U32(m_ChunkGridRes) + 1u;
		ASSERT(m_NumLods <= MAX_TERRAIN_LODS, "Too many terrain LODs. Increase MAX_TERRAIN_LODS.");

		// Height texture (CPU).
		m_HeightTexRef = assetManager.RegisterAsset<Texture>(m_CI.HeightPath);
		m_HeightTex = assetManager.LoadBlocking<Texture>(m_HeightTexRef);
		ASSERT(m_HeightTex && m_HeightTex->IsValid(), "Failed to load height Texture asset.");

		// Domain textures.
		ASSERT(!m_CI.DiffusePath.empty(), "Invalid diffuse texture path.");
		ASSERT(!m_CI.NormalPath.empty(), "Invalid normal texture path.");
		ASSERT(!m_CI.SlopePath.empty(), "Invalid slope texture path.");
		ASSERT(!m_CI.FlowPath.empty(), "Invalid flow texture path.");
		ASSERT(!m_CI.RockyPath.empty(), "Invalid rocky texture path.");
		ASSERT(!m_CI.SoilPath.empty(), "Invalid soil texture path.");
		ASSERT(!m_CI.VegetationPath.empty(), "Invalid vegetation texture path.");

		m_DiffuseTexRef = assetManager.RegisterAsset<Texture>(m_CI.DiffusePath);
		m_NormalTexRef = assetManager.RegisterAsset<Texture>(m_CI.NormalPath);
		m_SlopeTexRef = assetManager.RegisterAsset<Texture>(m_CI.SlopePath);
		m_FlowTexRef = assetManager.RegisterAsset<Texture>(m_CI.FlowPath);
		m_RockyTexRef = assetManager.RegisterAsset<Texture>(m_CI.RockyPath);
		m_SoilTexRef = assetManager.RegisterAsset<Texture>(m_CI.SoilPath);
		m_VegetationTexRef = assetManager.RegisterAsset<Texture>(m_CI.VegetationPath);

		// Materials.
		ASSERT(!m_CI.SoilMaterialPath.empty(), "Invalid soil material path.");
		ASSERT(!m_CI.RockyMaterialPath.empty(), "Invalid rocky material path.");
		ASSERT(!m_CI.m_GravelMaterialPath.empty(), "Invalid gravel material path.");
		m_SoilMaterialPath = m_CI.SoilMaterialPath;
		m_RockyMaterialPath = m_CI.RockyMaterialPath;
		m_GravelMaterialPath = m_CI.GravelMaterialPath;

		// CPU height array from mip0.
		buildHeightU16FromHeightTexture(*m_HeightTex);
		ASSERT(m_Width > 0u && m_Height > 0u, "Terrain height texture has invalid dimensions.");
		ASSERT(m_HeightU16.size() == size_t(m_Width) * size_t(m_Height), "Height data size mismatch.");

		// TerrainDrawConstantsBuffer (StructuredBuffer SRV, updated once per frame).
		{
			const float worldSizeX = GetWorldSizeX();
			const float worldSizeZ = GetWorldSizeZ();

			const uint32 numChunksX = uint32(std::ceil(worldSizeX / m_ChunkSize));
			const uint32 numChunksZ = uint32(std::ceil(worldSizeZ / m_ChunkSize));
			const uint32 maxInstances = Max(1u, numChunksX * numChunksZ);

			BufferDesc desc = {};
			desc.Name = "TerrainDrawConstantsBuffer";
			desc.Usage = USAGE_DYNAMIC;
			desc.BindFlags = BIND_SHADER_RESOURCE;
			desc.CPUAccessFlags = CPU_ACCESS_WRITE;
			desc.Mode = BUFFER_MODE_STRUCTURED;
			desc.ElementByteStride = sizeof(hlsl::TerrainDrawConstants);
			desc.Size = uint64(desc.ElementByteStride) * uint64(maxInstances);

			renderer.AddBuffer(STRING_HASH("TerrainDrawConstantsBuffer"), desc);
		}

		// VB (grid).
		{
			std::vector<TerrainVertex> verts;
			buildGridVertices(m_ChunkGridRes, verts);

			BufferDesc vb = {};
			vb.Name = "Terrain.Grid.VB";
			vb.Usage = USAGE_IMMUTABLE;
			vb.BindFlags = BIND_VERTEX_BUFFER;
			vb.Size = uint32(verts.size() * sizeof(TerrainVertex));

			BufferData init = {};
			init.pData = verts.data();
			init.DataSize = vb.Size;

			m_pGridVB = renderer.CreateVertexBuffer(vb, &init);
			ASSERT(m_pGridVB, "Create terrain VB failed.");
		}

		// IBs (LOD x StitchMask).
		for (uint32 lod = 0; lod < m_NumLods; ++lod)
		{
			const uint32 step = (1u << lod);
			ASSERT(step <= m_ChunkGridRes, "Invalid step derived from LOD.");

			for (uint32 mask = 0; mask < NUM_STITCH_MASKS; ++mask)
			{
				std::vector<uint16> idx;
				buildGridIndices(m_ChunkGridRes, step, uint8(mask), idx);

				m_LodIndexCount[lod][mask] = uint32(idx.size());

				BufferDesc ib = {};
				std::string name =
					"Terrain.Grid.IB.res" + std::to_string(m_ChunkGridRes) +
					".step" + std::to_string(step) +
					".mask" + std::to_string(mask);

				ib.Name = name.c_str();
				ib.Usage = USAGE_IMMUTABLE;
				ib.BindFlags = BIND_INDEX_BUFFER;
				ib.Size = uint32(idx.size() * sizeof(uint16));

				BufferData init = {};
				init.pData = idx.data();
				init.DataSize = ib.Size;

				m_pLodIB[lod][mask] = renderer.CreateIndexBuffer(ib, &init);
				ASSERT(m_pLodIB[lod][mask], "Create terrain IB failed (lod=%u mask=%u).", lod, mask);
			}
		}

		// GPU resources (textures + CB).
		{
			// Height (keep your behavior; upload full mip chain if available and format matches).
			bool uploadedFullHeightMip = false;
			if (m_HeightTex && m_HeightTex->IsValid() && m_HeightTex->GetFormat() == TEX_FORMAT_R16_UNORM)
			{
				uploadedFullHeightMip = uploadTextureAssetWithMips(
					renderer,
					assetManager,
					"TerrainHeight",
					m_HeightTexRef,
					"g_TerrainHeightTex");
			}

			if (!uploadedFullHeightMip)
			{
				TextureDesc desc = {};
				desc.Name = "TerrainHeight";
				desc.Type = RESOURCE_DIM_TEX_2D;
				desc.Width = m_Width;
				desc.Height = m_Height;
				desc.MipLevels = 1;
				desc.ArraySize = 1;
				desc.Format = TEX_FORMAT_R16_UNORM;
				desc.Usage = USAGE_DEFAULT;
				desc.BindFlags = BIND_SHADER_RESOURCE;

				TextureSubResData sr = {};
				sr.pData = m_HeightU16.data();
				sr.Stride = m_Width * sizeof(uint16);
				sr.DepthStride = 0;

				TextureData initData = {};
				initData.pSubResources = &sr;
				initData.NumSubresources = 1;

				renderer.AddTexture(STRING_HASH("TerrainHeight"), desc, &initData);
				renderer.RegisterStaticTextureResource("g_TerrainHeightTex", STRING_HASH("TerrainHeight"));
			}

			// Domain textures (upload all mips).
			uploadTextureAssetWithMips(renderer, assetManager, "TerrainDiffuse", m_DiffuseTexRef, "g_TerrainDiffuseTex");
			uploadTextureAssetWithMips(renderer, assetManager, "TerrainNormal", m_NormalTexRef, "g_TerrainNormalTex");
			uploadTextureAssetWithMips(renderer, assetManager, "TerrainSlope", m_SlopeTexRef, "g_TerrainSlopeTex");
			uploadTextureAssetWithMips(renderer, assetManager, "TerrainFlow", m_FlowTexRef, "g_TerrainFlowTex");
			uploadTextureAssetWithMips(renderer, assetManager, "TerrainRocky", m_RockyTexRef, "g_TerrainRockyTex");
			uploadTextureAssetWithMips(renderer, assetManager, "TerrainSoil", m_SoilTexRef, "g_TerrainSoilTex");
			uploadTextureAssetWithMips(renderer, assetManager, "TerrainVegetation", m_VegetationTexRef, "g_TerrainVegetationTex");

			// Terrain constants CB.
			{
				BufferDesc cb = {};
				cb.Name = "TerrainCB";
				cb.Usage = USAGE_DYNAMIC;
				cb.BindFlags = BIND_UNIFORM_BUFFER;
				cb.CPUAccessFlags = CPU_ACCESS_WRITE;
				cb.Size = sizeof(hlsl::TerrainConstants);

				renderer.AddBuffer(STRING_HASH("TerrainCB"), cb);
				renderer.RegisterStaticBufferCBV("TERRAIN_CONSTANTS", STRING_HASH("TerrainCB"));
			}
		}

		// Material.
		{
			auto getLastFolderName = [](const std::string& path) -> std::string
				{
					ASSERT(!path.empty(), "Path is empty.");

					std::string p = path;
					while (!p.empty() && (p.back() == '/' || p.back() == '\\'))
					{
						p.pop_back();
					}
					ASSERT(!p.empty(), "Path is empty.");

					const size_t slash0 = p.find_last_of("/\\");
					if (slash0 == std::string::npos)
					{
						return p;
					}
					return p.substr(slash0 + 1);
				};

			auto joinPath = [](const std::string& a, const std::string& b) -> std::string
				{
					ASSERT(!b.empty(), "Path segment is empty.");

					const char last = a.empty() ? '\0' : a.back();
					if (last == '/' || last == '\\')
					{
						return a + b;
					}
					return a + "/" + b;
				};

			struct TerrainLayerPaths final
			{
				std::string BaseColor;
				std::string Normal;
				std::string Roughness;
				std::string AmbientOcclusion;
				std::string Displacement;
			};

			auto buildTerrainLayerPaths = [&](const std::string& folderPath, bool bNormalDX) -> TerrainLayerPaths
				{
					TerrainLayerPaths out = {};

					const std::string folderName = getLastFolderName(folderPath);
					ASSERT(!folderName.empty(), "Terrain layer folder name is empty.");

					const std::string normalSuffix = bNormalDX ? "_NormalDX.png" : "_NormalGL.png";

					out.BaseColor = joinPath(folderPath, folderName + "_Color.png");
					out.Normal = joinPath(folderPath, folderName + normalSuffix);
					out.Roughness = joinPath(folderPath, folderName + "_Roughness.png");
					out.AmbientOcclusion = joinPath(folderPath, folderName + "_AmbientOcclusion.png");
					out.Displacement = joinPath(folderPath, folderName + "_Displacement.png");
					return out;
				};

			renderer.RegisterMaterialTemplate("Terrain", m_TerrainVS, m_TerrainPS, MATERIAL_BLEND_MODE_OPAQUE);

			m_TerrainMaterialId = MaterialManager::GetInstance()->CreateMaterial("TerrainMaterial", "Terrain");
			Material& mat = MaterialManager::GetInstance()->GetMaterial(m_TerrainMaterialId);

			// PBR defaults.
			mat.SetFloat4("g_BaseColorFactor", float4(1.0f, 1.0f, 1.0f, 1.0f));
			mat.SetFloat3("g_EmissiveFactor", float3(0.0f, 0.0f, 0.0f));
			mat.SetFloat("g_EmissiveIntensity", 0.0f);
			mat.SetFloat("g_RoughnessFactor", 1.0f);
			mat.SetFloat("g_NormalScale", 1.0f);
			mat.SetFloat("g_OcclusionStrength", 1.0f);
			mat.SetFloat("g_AlphaCutoff", 0.5f);
			mat.SetFloat("g_MetallicFactor", 0.0f);
			mat.SetUint("g_MaterialFlags", 0u);

			// Terrain layer textures (Soil + Rocky + Gravel)
			const bool bNormalDX = true;
			const TerrainLayerPaths soil = buildTerrainLayerPaths(m_SoilMaterialPath, bNormalDX);
			const TerrainLayerPaths rocky = buildTerrainLayerPaths(m_RockyMaterialPath, bNormalDX);
			const TerrainLayerPaths gravel = buildTerrainLayerPaths(m_GravelMaterialPath, bNormalDX);

			// Soil
			mat.SetTextureAssetRef("g_SoilBaseColorTex", assetManager.RegisterAsset<Texture>(soil.BaseColor));
			mat.SetTextureAssetRef("g_SoilNormalTex", assetManager.RegisterAsset<Texture>(soil.Normal));
			mat.SetTextureAssetRef("g_SoilRoughnessTex", assetManager.RegisterAsset<Texture>(soil.Roughness));
			mat.SetTextureAssetRef("g_SoilAmbientOcclusionTex", assetManager.RegisterAsset<Texture>(soil.AmbientOcclusion));
			mat.SetTextureAssetRef("g_SoilDisplacementTex", assetManager.RegisterAsset<Texture>(soil.Displacement));

			// Rocky
			mat.SetTextureAssetRef("g_RockyBaseColorTex", assetManager.RegisterAsset<Texture>(rocky.BaseColor));
			mat.SetTextureAssetRef("g_RockyNormalTex", assetManager.RegisterAsset<Texture>(rocky.Normal));
			mat.SetTextureAssetRef("g_RockyRoughnessTex", assetManager.RegisterAsset<Texture>(rocky.Roughness));
			mat.SetTextureAssetRef("g_RockyAmbientOcclusionTex", assetManager.RegisterAsset<Texture>(rocky.AmbientOcclusion));
			mat.SetTextureAssetRef("g_RockyDisplacementTex", assetManager.RegisterAsset<Texture>(rocky.Displacement));

			// Gravel
			mat.SetTextureAssetRef("g_GravelBaseColorTex", assetManager.RegisterAsset<Texture>(gravel.BaseColor));
			mat.SetTextureAssetRef("g_GravelNormalTex", assetManager.RegisterAsset<Texture>(gravel.Normal));
			mat.SetTextureAssetRef("g_GravelRoughnessTex", assetManager.RegisterAsset<Texture>(gravel.Roughness));
			mat.SetTextureAssetRef("g_GravelAmbientOcclusionTex", assetManager.RegisterAsset<Texture>(gravel.AmbientOcclusion));
			mat.SetTextureAssetRef("g_GravelDisplacementTex", assetManager.RegisterAsset<Texture>(gravel.Displacement));

			// mat.SetCullMode(CULL_MODE_NONE);

			// Per-draw constants table SRV.
			mat.SetBufferResource("g_TerrainDrawConstants", STRING_HASH("TerrainDrawConstantsBuffer"));
		}
	}

	void TerrainSystem::Cleanup()
	{
		m_CI = {};

		m_Width = 0u;
		m_Height = 0u;

		m_ChunkSize = 64.0f;
		m_CellSize = 1.0f;
		m_ChunkGridRes = 64u;

		m_WorldSpacing = 1.0f;

		m_HeightScale = 100.0f;
		m_HeightOffset = 0.0f;

		m_bCenterXZ = true;

		m_HeightTexRef = {};
		m_DiffuseTexRef = {};
		m_NormalTexRef = {};
		m_SlopeTexRef = {};
		m_FlowTexRef = {};
		m_RockyTexRef = {};
		m_SoilTexRef = {};
		m_VegetationTexRef = {};

		m_HeightTex.Reset();

		m_HeightU16.clear();
		m_HeightU16.shrink_to_fit();

		m_pGridVB.Release();

		for (uint32 lod = 0; lod < MAX_TERRAIN_LODS; ++lod)
		{
			for (uint32 mask = 0; mask < NUM_STITCH_MASKS; ++mask)
			{
				m_pLodIB[lod][mask].Release();
				m_LodIndexCount[lod][mask] = 0u;
			}
		}

		m_SceneHandles.clear();
		m_SoilMaterialPath.clear();
		m_RockyMaterialPath.clear();
		m_TerrainMaterialId = 0;
		m_NumLods = 0u;
	}

	// Per-frame Update
// Per-frame Update (Instanced batching)
	void TerrainSystem::Update(Renderer& renderer, RenderScene* pScene, const View& view)
	{
		ASSERT(pScene, "RenderScene is null.");
		ASSERT(m_Width > 0u && m_Height > 0u, "Invalid terrain height dimensions.");
		ASSERT(m_NumLods > 0u, "Invalid number of LODs.");

		const float worldOriginX = GetWorldOriginX();
		const float worldOriginZ = GetWorldOriginZ();
		const float worldSizeX = GetWorldSizeX();
		const float worldSizeZ = GetWorldSizeZ();

		const uint32 numChunksX = uint32(std::ceil(worldSizeX / m_ChunkSize));
		const uint32 numChunksZ = uint32(std::ceil(worldSizeZ / m_ChunkSize));
		ASSERT(numChunksX > 0u && numChunksZ > 0u, "Number of chunks must be > 0.");

		// ---------------------------------------------------------------------
		// Update TerrainCB once per frame.
		// ---------------------------------------------------------------------
		{
			hlsl::TerrainConstants cb = {};
			cb.WorldOriginXZ = float2{ worldOriginX, worldOriginZ };
			cb.WorldSizeXZ = float2{ worldSizeX, worldSizeZ };

			cb.ChunkSize = m_ChunkSize;
			cb.InvChunkSize = 1.0f / m_ChunkSize;

			cb.WorldSpacing = m_WorldSpacing;
			cb.InvWorldSpacing = 1.0f / m_WorldSpacing;

			cb.HeightTexelSize = float2
			{
				1.0f / m_Width,
				1.0f / m_Height
			};

			cb.HeightScale = m_HeightScale;
			cb.HeightOffset = m_HeightOffset;

			cb.CenterXZ = m_bCenterXZ ? 1u : 0u;
			cb.NormalUpBias = 2.0f;

			cb.ChunkGridRes = m_ChunkGridRes;
			cb.InvChunkGridRes = 1.0f / Max(1.0f, float(m_ChunkGridRes));

			std::vector<uint8> bytes(sizeof(cb));
			std::memcpy(bytes.data(), &cb, sizeof(cb));
			renderer.UpdateBuffer(STRING_HASH("TerrainCB"), std::move(bytes));
		}

		auto idx2D = [&](uint32 cx, uint32 cz) -> uint32
		{
			return cz * numChunksX + cx;
		};

		// ---------------------------------------------------------------------
		// LOD ranges (kept).
		// ---------------------------------------------------------------------
		struct LodRange final { float End; };
		const LodRange lodRanges[5] =
		{
			{ 128.0f },
			{ 256.0f },
			{ 512.0f },
			{ 1024.0f },
			{ FLT_MAX },
		};

		auto selectLodOnly = [&](float dist) -> uint32
		{
			uint32 lod = 0u;
			if (dist < lodRanges[0].End) { lod = 0u; }
			else if (dist < lodRanges[1].End) { lod = 1u; }
			else if (dist < lodRanges[2].End) { lod = 2u; }
			else if (dist < lodRanges[3].End) { lod = 3u; }
			else { lod = 4u; }

			return Clamp(lod, 0u, Max(0u, m_NumLods - 1u));
		};

		auto morphForClampedLod = [&](float dist, uint32 lod) -> float
		{
			ASSERT(lod + 1 < m_NumLods, "LOD must have a valid next LOD for morphing.");

			const uint32 idx = Min(lod, 3u);
			const float d1 = lodRanges[idx].End;
			const float d0 = d1 * 0.85f;

			const float t = Clamp01((dist - d0) / (d1 - d0));
			return t * t * (3.0f - 2.0f * t);
		};

		// ---------------------------------------------------------------------
		// Build LOD grid (per-chunk chosen lod).
		// ---------------------------------------------------------------------
		std::vector<uint8> lodGrid;
		lodGrid.resize(size_t(numChunksX) * size_t(numChunksZ), uint8(Max(0u, m_NumLods - 1u)));

		{
			const float3 cam = view.CameraPosition;

			for (uint32 cz = 0u; cz < numChunksZ; ++cz)
			{
				for (uint32 cx = 0u; cx < numChunksX; ++cx)
				{
					const float chunkOriginX = worldOriginX + float(cx) * m_ChunkSize;
					const float chunkOriginZ = worldOriginZ + float(cz) * m_ChunkSize;

					const float remainX = (worldOriginX + worldSizeX) - chunkOriginX;
					const float remainZ = (worldOriginZ + worldSizeZ) - chunkOriginZ;

					const float chunkSizeX = (remainX > 0.0f) ? Min(m_ChunkSize, remainX) : 0.0f;
					const float chunkSizeZ = (remainZ > 0.0f) ? Min(m_ChunkSize, remainZ) : 0.0f;

					const float cxw = chunkOriginX + 0.5f * chunkSizeX;
					const float czw = chunkOriginZ + 0.5f * chunkSizeZ;

					const float dx = cam.x - cxw;
					const float dz = cam.z - czw;
					const float dist = std::sqrt(dx * dx + dz * dz);

					const uint32 lod = selectLodOnly(dist);
					lodGrid[idx2D(cx, cz)] = uint8(lod);
				}
			}
		}

		// ---------------------------------------------------------------------
		// Neighbor clamp (LOD diff <= 1).
		// ---------------------------------------------------------------------
		{
			bool changed = true;
			uint32 iter = 0u;

			while (changed && iter++ < 32u)
			{
				changed = false;

				for (uint32 cz = 0u; cz < numChunksZ; ++cz)
				{
					for (uint32 cx = 0u; cx < numChunksX; ++cx)
					{
						uint8& a = lodGrid[idx2D(cx, cz)];

						auto clampPair = [&](uint32 nx, uint32 nz)
						{
							uint8& b = lodGrid[idx2D(nx, nz)];

							if (b > uint8(a + 1u)) { b = uint8(a + 1u); changed = true; }
							if (a > uint8(b + 1u)) { a = uint8(b + 1u); changed = true; }
						};

						if (cx > 0u) { clampPair(cx - 1u, cz); }
						if (cx + 1u < numChunksX) { clampPair(cx + 1u, cz); }
						if (cz > 0u) { clampPair(cx, cz - 1u); }
						if (cz + 1u < numChunksZ) { clampPair(cx, cz + 1u); }
					}
				}
			}
		}

		const float yMin = m_HeightOffset;
		const float yMax = m_HeightOffset + m_HeightScale;
		const float3 cam = view.CameraPosition;

		auto nLod = [&](int32 nx, int32 nz, uint32 lodHere) -> uint32
		{
			if (nx < 0 || nz < 0 || nx >= int32(numChunksX) || nz >= int32(numChunksZ))
			{
				return lodHere;
			}
			return uint32(lodGrid[idx2D(uint32(nx), uint32(nz))]);
		};

		// ---------------------------------------------------------------------
		// Instanced batching:
		//  - Group chunks by (LOD, StitchMask). Material is single (m_TerrainMaterialId).
		//  - Concatenate batches into TerrainDrawConstantsBuffer contiguously.
		//  - Register ONLY one TerrainObject per batch with InstanceCount > 1.
		// ---------------------------------------------------------------------
		struct TerrainBatch final
		{
			std::vector<hlsl::TerrainDrawConstants> Instances;
			Box BoundsWS;          // union bounds in world space (stored as local bounds with Identity world)
			bool bHasBounds = false;
			uint32 StartInstanceLocation = 0;
			uint32 InstanceCount = 0;
			uint32 Lod = 0;
			uint8  Mask = 0;
		};

		const uint32 maxLods = m_NumLods;
		static constexpr uint32 MASK_COUNT = 16;

		// [lod][mask]
		std::vector<TerrainBatch> batches;
		batches.resize(size_t(maxLods) * size_t(MASK_COUNT));

		auto batchAt = [&](uint32 lod, uint8 mask) -> TerrainBatch&
		{
			ASSERT(lod < maxLods, "batchAt: lod OOB.");
			ASSERT(mask < MASK_COUNT, "batchAt: mask OOB.");
			return batches[size_t(lod) * MASK_COUNT + size_t(mask)];
		};

		auto unionChunkBoundsWS = [&](TerrainBatch& b, float originX, float originZ, float sizeX, float sizeZ)
		{
			// chunk bounds in world space (min/max)
			const float3 mn = float3{ originX, yMin, originZ };
			const float3 mx = float3{ originX + sizeX, yMax, originZ + sizeZ };

			if (!b.bHasBounds)
			{
				b.BoundsWS = Box(mn, mx);
				b.bHasBounds = true;
			}
			else
			{
				b.BoundsWS.Encapsulate(mn);
				b.BoundsWS.Encapsulate(mx);
			}
		};

		// Fill batches
		for (uint32 cz = 0u; cz < numChunksZ; ++cz)
		{
			for (uint32 cx = 0u; cx < numChunksX; ++cx)
			{
				const float chunkOriginX = worldOriginX + float(cx) * m_ChunkSize;
				const float chunkOriginZ = worldOriginZ + float(cz) * m_ChunkSize;

				const float remainX = (worldOriginX + worldSizeX) - chunkOriginX;
				const float remainZ = (worldOriginZ + worldSizeZ) - chunkOriginZ;

				const float chunkSizeX = (remainX > 0.0f) ? Min(m_ChunkSize, remainX) : 0.0f;
				const float chunkSizeZ = (remainZ > 0.0f) ? Min(m_ChunkSize, remainZ) : 0.0f;

				if (chunkSizeX <= EPSILON || chunkSizeZ <= EPSILON)
					continue;

				const uint32 lod = uint32(lodGrid[idx2D(cx, cz)]);
				ASSERT(lod < m_NumLods, "Invalid LOD.");

				uint8 mask = STITCH_NONE;
				if (nLod(int32(cx) - 1, int32(cz), lod) > lod) { mask |= STITCH_LEFT; }
				if (nLod(int32(cx) + 1, int32(cz), lod) > lod) { mask |= STITCH_RIGHT; }
				if (nLod(int32(cx), int32(cz) - 1, lod) > lod) { mask |= STITCH_BOTTOM; }
				if (nLod(int32(cx), int32(cz) + 1, lod) > lod) { mask |= STITCH_TOP; }

				// optional morph (currently unused)
				{
					const float cxw = chunkOriginX + 0.5f * chunkSizeX;
					const float czw = chunkOriginZ + 0.5f * chunkSizeZ;
					const float dx = cam.x - cxw;
					const float dz = cam.z - czw;
					const float dist = std::sqrt(dx * dx + dz * dz);
					const float morph = (lod + 1 < m_NumLods) ? morphForClampedLod(dist, lod) : 0.0f;
					(void)morph;
				}

				hlsl::TerrainDrawConstants dc = {};
				dc.ChunkOriginXZ = float2{ chunkOriginX, chunkOriginZ };
				dc.LodIndex = lod;
				dc.ChunkSizeXZ = float2{ chunkSizeX, chunkSizeZ };
				dc.InvChunkSizeXZ = float2
				{
					(chunkSizeX > EPSILON) ? (1.0f / chunkSizeX) : 0.0f,
					(chunkSizeZ > EPSILON) ? (1.0f / chunkSizeZ) : 0.0f
				};

				TerrainBatch& b = batchAt(lod, mask);
				b.Lod = lod;
				b.Mask = mask;

				b.Instances.emplace_back(dc);
				unionChunkBoundsWS(b, chunkOriginX, chunkOriginZ, chunkSizeX, chunkSizeZ);
			}
		}

		// Count total instances and assign StartInstanceLocation contiguously
		uint32 totalInstances = 0u;
		for (uint32 lod = 0; lod < maxLods; ++lod)
		{
			for (uint32 mask = 0; mask < MASK_COUNT; ++mask)
			{
				TerrainBatch& b = batchAt(lod, uint8(mask));
				if (b.Instances.empty())
					continue;

				b.StartInstanceLocation = totalInstances;
				b.InstanceCount = static_cast<uint32>(b.Instances.size());
				totalInstances += b.InstanceCount;
			}
		}

		if (totalInstances == 0u)
			return;

		// Build contiguous instance table
		std::vector<hlsl::TerrainDrawConstants> instanceTable;
		instanceTable.resize(totalInstances);

		for (uint32 lod = 0; lod < maxLods; ++lod)
		{
			for (uint32 mask = 0; mask < MASK_COUNT; ++mask)
			{
				TerrainBatch& b = batchAt(lod, uint8(mask));
				if (b.Instances.empty())
					continue;

				std::memcpy(
					instanceTable.data() + b.StartInstanceLocation,
					b.Instances.data(),
					size_t(b.InstanceCount) * sizeof(hlsl::TerrainDrawConstants));
			}
		}

		// Upload instance table once per frame.
		{
			std::vector<uint8> byteBuffer(instanceTable.size() * sizeof(hlsl::TerrainDrawConstants));
			std::memcpy(byteBuffer.data(), instanceTable.data(), byteBuffer.size());
			renderer.UpdateBuffer(STRING_HASH("TerrainDrawConstantsBuffer"), std::move(byteBuffer));
		}

		// Rebuild scene terrain objects: now "one object per batch"
		for (auto h : m_SceneHandles)
		{
			pScene->RemoveTerrain(h);
		}
		m_SceneHandles.clear();

		// Add per-batch TerrainObject (instanced)
		for (uint32 lod = 0; lod < maxLods; ++lod)
		{
			for (uint32 mask = 0; mask < MASK_COUNT; ++mask)
			{
				TerrainBatch& b = batchAt(lod, uint8(mask));
				if (b.Instances.empty())
					continue;

				ASSERT(b.bHasBounds, "TerrainBatch should have bounds.");

				RenderScene::TerrainObject obj = {};
				obj.IndexType = VT_UINT16;

				obj.VertexBuffer = m_pGridVB;
				obj.IndexBuffer = m_pLodIB[lod][mask];
				obj.IndexCount = m_LodIndexCount[lod][mask];

				obj.InstanceCount = b.InstanceCount;
				obj.StartInstanceLocation = b.StartInstanceLocation;

				obj.MaterialId = m_TerrainMaterialId;
				obj.bCastShadow = true;

				// Visibility bounds:
				// RenderScene expects LocalBounds + World. We store bounds in WS and use Identity world.
				obj.World = Matrix4x4::Identity();
				obj.LocalBounds = b.BoundsWS;

				Handle<RenderScene::TerrainObject> h = pScene->AddTerrain(obj);
				m_SceneHandles.push_back(h);
			}
		}
	}

	float TerrainSystem::GetWorldOriginX() const noexcept
	{
		if (m_bCenterXZ)
		{
			return -0.5f * GetWorldSizeX();
		}
		return 0.0f;
	}

	float TerrainSystem::GetWorldOriginZ() const noexcept
	{
		if (m_bCenterXZ)
		{
			return -0.5f * GetWorldSizeZ();
		}
		return 0.0f;
	}

	float2 TerrainSystem::WorldXZToDomainUV(const float2& worldXZ) const noexcept
	{
		const float originX = GetWorldOriginX();
		const float originZ = GetWorldOriginZ();

		const float sizeX = GetWorldSizeX();
		const float sizeZ = GetWorldSizeZ();

		const float u = (worldXZ.x - originX) / sizeX;
		const float v = (worldXZ.y - originZ) / sizeZ;
		return float2{ Clamp01(u), Clamp01(v) };
	}

	float2 TerrainSystem::DomainUVToWorldXZ(const float2& uv) const noexcept
	{
		const float originX = GetWorldOriginX();
		const float originZ = GetWorldOriginZ();

		const float sizeX = GetWorldSizeX();
		const float sizeZ = GetWorldSizeZ();

		const float x = originX + Clamp01(uv.x) * sizeX;
		const float z = originZ + Clamp01(uv.y) * sizeZ;
		return float2{ x, z };
	}

	float TerrainSystem::GetNormalizedHeightAt(uint32 x, uint32 z) const
	{
		ASSERT(x < m_Width, "X out of range.");
		ASSERT(z < m_Height, "Z out of range.");
		return u16ToNormalized(m_HeightU16[size_t(z) * size_t(m_Width) + x]);
	}

	float TerrainSystem::GetWorldHeightAt(uint32 x, uint32 z) const
	{
		return m_HeightOffset + GetNormalizedHeightAt(x, z) * m_HeightScale;
	}

	float TerrainSystem::SampleNormalizedHeight(float worldX, float worldZ) const
	{
		ASSERT(m_WorldSpacing > 0.0f, "Spacing must be > 0.");

		const float originX = GetWorldOriginX();
		const float originZ = GetWorldOriginZ();

		const float gx = (worldX - originX) / m_WorldSpacing;
		const float gz = (worldZ - originZ) / m_WorldSpacing;

		const float maxX = float(m_Width - 1u);
		const float maxZ = float(m_Height - 1u);

		const float x = Clamp(gx, 0.0f, maxX);
		const float z = Clamp(gz, 0.0f, maxZ);

		const uint32 x0 = uint32(std::floor(x));
		const uint32 z0 = uint32(std::floor(z));

		const uint32 x1 = (x0 + 1u < m_Width) ? (x0 + 1u) : x0;
		const uint32 z1 = (z0 + 1u < m_Height) ? (z0 + 1u) : z0;

		const float tx = x - float(x0);
		const float tz = z - float(z0);

		const float h00 = GetNormalizedHeightAt(x0, z0);
		const float h10 = GetNormalizedHeightAt(x1, z0);
		const float h01 = GetNormalizedHeightAt(x0, z1);
		const float h11 = GetNormalizedHeightAt(x1, z1);

		const float hx0 = h00 + (h10 - h00) * tx;
		const float hx1 = h01 + (h11 - h01) * tx;
		return Clamp01(hx0 + (hx1 - hx0) * tz);
	}

	float TerrainSystem::SampleWorldHeight(float worldX, float worldZ) const
	{
		return m_HeightOffset + SampleNormalizedHeight(worldX, worldZ) * m_HeightScale;
	}

	void TerrainSystem::BuildPhysicsHeightSamples(std::vector<float>& outHeightsWorldMeters) const
	{
		outHeightsWorldMeters.resize(m_HeightU16.size());
		for (size_t i = 0; i < m_HeightU16.size(); ++i)
		{
			const float n = float(m_HeightU16[i]) / 65535.0f;
			outHeightsWorldMeters[i] = n * m_HeightScale + m_HeightOffset;
		}
	}

	void TerrainSystem::buildHeightU16FromHeightTexture(const Texture& heightTex)
	{
		ASSERT(heightTex.IsValid(), "Height texture is invalid.");
		ASSERT(!heightTex.GetMips().empty(), "Height texture has no mips.");

		const uint32 w = heightTex.GetWidth();
		const uint32 h = heightTex.GetHeight();
		ASSERT(w > 0u && h > 0u, "Invalid height texture dimensions.");

		const TEXTURE_FORMAT fmt = heightTex.GetFormat();
		const TextureFormatAttribs& a = GetTextureFormatAttribs(fmt);

		ASSERT(a.NumComponents > 0u && a.ComponentSize > 0u, "Invalid format attribs.");
		ASSERT(a.ComponentType != COMPONENT_TYPE_COMPRESSED, "Compressed formats are not supported.");

		const uint32 bytesPerPixel = a.NumComponents * a.ComponentSize;

		const TextureMip& mip0 = heightTex.GetMips()[0];
		ASSERT(mip0.Width == w && mip0.Height == h, "Mip0 size mismatch.");
		ASSERT(!mip0.Data.empty(), "Mip0 data empty.");

		const uint64 expectedMinBytes = uint64(w) * uint64(h) * uint64(bytesPerPixel);
		ASSERT(uint64(mip0.Data.size()) >= expectedMinBytes, "Mip0 data smaller than expected.");

		m_Width = w;
		m_Height = h;
		m_HeightU16.assign(size_t(w) * size_t(h), 0u);

		const uint8* src = mip0.Data.data();
		const uint64 rowStride = uint64(w) * uint64(bytesPerPixel);

		if (a.ComponentSize == 1u)
		{
			const float inv = 1.0f / 255.0f;
			for (uint32 z = 0u; z < h; ++z)
			{
				const uint8* row = src + size_t(z) * size_t(rowStride);
				for (uint32 x = 0u; x < w; ++x)
				{
					const uint8* px = row + size_t(x) * bytesPerPixel;
					const uint8 r = px[0];
					m_HeightU16[size_t(z) * size_t(w) + x] = normalizedToU16(Clamp01(float(r) * inv));
				}
			}
		}
		else if (a.ComponentSize == 2u)
		{
			const float inv = 1.0f / 65535.0f;
			for (uint32 z = 0u; z < h; ++z)
			{
				const uint8* rowBytes = src + size_t(z) * size_t(rowStride);
				for (uint32 x = 0u; x < w; ++x)
				{
					const uint16* px = reinterpret_cast<const uint16*>(rowBytes + size_t(x) * bytesPerPixel);
					const uint16 r = px[0];
					m_HeightU16[size_t(z) * size_t(w) + x] = normalizedToU16(Clamp01(float(r) * inv));
				}
			}
		}
		else if (a.ComponentSize == 4u)
		{
			for (uint32 z = 0u; z < h; ++z)
			{
				const uint8* rowBytes = src + size_t(z) * size_t(rowStride);
				for (uint32 x = 0u; x < w; ++x)
				{
					const float* px = reinterpret_cast<const float*>(rowBytes + size_t(x) * bytesPerPixel);
					const float r = px[0];
					m_HeightU16[size_t(z) * size_t(w) + x] = normalizedToU16(Clamp01(r));
				}
			}
		}
		else
		{
			ASSERT(false, "Unsupported component size.");
		}
	}

	void TerrainSystem::buildGridVertices(uint32 chunkGridRes, std::vector<TerrainVertex>& outVerts) const
	{
		const uint32 vertsPerSide = chunkGridRes + 1u;

		outVerts.clear();
		outVerts.reserve(size_t(vertsPerSide) * size_t(vertsPerSide));

		for (uint32 z = 0; z < vertsPerSide; ++z)
		{
			for (uint32 x = 0; x < vertsPerSide; ++x)
			{
				const float u = float(x) / float(chunkGridRes);
				const float v = float(z) / float(chunkGridRes);

				TerrainVertex vtx = {};
				vtx.Pos = float3{ float(x), 0.0f, float(z) };
				vtx.UV = float2{ u, v };
				vtx.Normal = float3{ 0.0f, 1.0f, 0.0f };
				vtx.Tangent = float3{ 1.0f, 0.0f, 0.0f };
				outVerts.emplace_back(vtx);
			}
		}
	}

	void TerrainSystem::buildGridIndices(
		uint32 chunkGridRes,
		uint32 step,
		uint8  stitchMask,
		std::vector<uint16>& outIdxU16) const
	{
		outIdxU16.clear();

		ASSERT(chunkGridRes >= 2u, "chunkGridRes too small.");
		ASSERT(step >= 1u, "Invalid step.");
		ASSERT((chunkGridRes % step) == 0u, "Step must divide chunkGridRes.");

		const uint32 vertsPerSide = chunkGridRes + 1u;

		// Enforce uint16 indexing.
		ASSERT(vertsPerSide * vertsPerSide <= 65535u,
			"Grid is too large for uint16 indices. Reduce resolution (ChunkGridRes) or add uint32 index path.");

		auto vid = [&](uint32 gx, uint32 gz) -> uint16
			{
				ASSERT(gx <= chunkGridRes && gz <= chunkGridRes, "Grid index out of range.");
				return uint16(gz * vertsPerSide + gx);
			};

		// Winding stabilizer (XZ cross sign).
		const int32 refSign = +1;

		auto cross2XZSign = [&](uint32 ax, uint32 az, uint32 bx, uint32 bz, uint32 cx, uint32 cz) -> int32
			{
				const int32 x1 = int32(bx) - int32(ax);
				const int32 z1 = int32(bz) - int32(az);
				const int32 x2 = int32(cx) - int32(ax);
				const int32 z2 = int32(cz) - int32(az);

				const int32 c = x1 * z2 - z1 * x2;
				return (c > 0) ? +1 : (c < 0 ? -1 : 0);
			};

		auto pushTriGrid = [&](uint32 ax, uint32 az, uint32 bx, uint32 bz, uint32 cx, uint32 cz)
			{
				uint16 a = vid(ax, az);
				uint16 b = vid(bx, bz);
				uint16 c = vid(cx, cz);

				const int32 sgn = cross2XZSign(ax, az, bx, bz, cx, cz);
				if (sgn != 0 && sgn != refSign)
				{
					std::swap(b, c);
				}

				outIdxU16.push_back(a);
				outIdxU16.push_back(b);
				outIdxU16.push_back(c);
			};

		auto pushQuadStd = [&](uint32 x0, uint32 z0)
			{
				const uint32 x1 = x0 + step;
				const uint32 z1 = z0 + step;

				pushTriGrid(x0, z0, x1, z0, x0, z1);
				pushTriGrid(x1, z0, x1, z1, x0, z1);
			};

		if (step == chunkGridRes)
		{
			outIdxU16.reserve(6);
			pushQuadStd(0, 0);
			ASSERT((outIdxU16.size() % 3u) == 0u, "Index count must be multiple of 3.");
			return;
		}

		const bool bStitchL = (stitchMask & STITCH_LEFT) != 0;
		const bool bStitchR = (stitchMask & STITCH_RIGHT) != 0;
		const bool bStitchB = (stitchMask & STITCH_BOTTOM) != 0;
		const bool bStitchT = (stitchMask & STITCH_TOP) != 0;

		const uint32 s = step;
		const uint32 s2 = step * 2u;

		outIdxU16.reserve(size_t(chunkGridRes) * size_t(chunkGridRes) * 6u);

		// Interior (exclude outer ring).
		const uint32 quadsInThisLod = chunkGridRes / step;
		if (quadsInThisLod > 2u)
		{
			for (uint32 qz = 1u; qz < quadsInThisLod - 1u; ++qz)
			{
				for (uint32 qx = 1u; qx < quadsInThisLod - 1u; ++qx)
				{
					pushQuadStd(qx * step, qz * step);
				}
			}
		}

		auto emitStitchStripX = [&](uint32 zBoundary, uint32 zInner, uint32 xBegin, uint32 xEnd)
			{
				for (uint32 x = xBegin; (x + s2) <= xEnd; x += s2)
				{
					const uint32 x0 = x;
					const uint32 x1 = x + s;
					const uint32 x2 = x + s2;

					pushTriGrid(x0, zBoundary, x2, zBoundary, x1, zInner);
					pushTriGrid(x0, zBoundary, x1, zInner, x0, zInner);
					pushTriGrid(x2, zBoundary, x2, zInner, x1, zInner);
				}
			};

		auto emitStitchStripZ = [&](uint32 xBoundary, uint32 xInner, uint32 zBegin, uint32 zEnd)
			{
				for (uint32 z = zBegin; (z + s2) <= zEnd; z += s2)
				{
					const uint32 z0 = z;
					const uint32 z1 = z + s;
					const uint32 z2 = z + s2;

					pushTriGrid(xBoundary, z0, xBoundary, z2, xInner, z1);
					pushTriGrid(xBoundary, z0, xInner, z1, xInner, z0);
					pushTriGrid(xBoundary, z2, xInner, z2, xInner, z1);
				}
			};

		const bool bCornerBL = bStitchB && bStitchL;
		const bool bCornerBR = bStitchB && bStitchR;
		const bool bCornerTL = bStitchT && bStitchL;
		const bool bCornerTR = bStitchT && bStitchR;

		const uint32 Q = chunkGridRes;

		// Corner patches.
		if (bCornerBL)
		{
			uint32 x0 = 0u;   uint32 z0 = 0u;
			uint32 x1 = x0 + s; uint32 z1 = z0 + s;
			uint32 x2 = x0 + s2; uint32 z2 = z0 + s2;
			pushTriGrid(x0, z0, x1, z1, x0, z2);
			pushTriGrid(x0, z0, x1, z1, x2, z0);
			pushTriGrid(x2, z0, x1, z1, x2, z1);
			pushTriGrid(x0, z2, x1, z1, x1, z2);
		}
		if (bCornerBR)
		{
			uint32 x0 = Q - s2; uint32 z0 = 0u;
			uint32 x1 = x0 + s; uint32 z1 = z0 + s;
			uint32 x2 = x0 + s2; uint32 z2 = z0 + s2;
			pushTriGrid(x0, z0, x1, z1, x2, z0);
			pushTriGrid(x2, z2, x1, z1, x2, z0);
			pushTriGrid(x0, z0, x1, z1, x0, z1);
			pushTriGrid(x1, z2, x1, z1, x2, z2);
		}
		if (bCornerTL)
		{
			uint32 x0 = 0u;   uint32 z0 = Q - s2;
			uint32 x1 = x0 + s; uint32 z1 = z0 + s;
			uint32 x2 = x0 + s2; uint32 z2 = z0 + s2;
			pushTriGrid(x0, z0, x1, z1, x0, z2);
			pushTriGrid(x2, z2, x1, z1, x0, z2);
			pushTriGrid(x0, z0, x1, z1, x1, z0);
			pushTriGrid(x2, z1, x1, z1, x2, z2);
		}
		if (bCornerTR)
		{
			uint32 x0 = Q - s2; uint32 z0 = Q - s2;
			uint32 x1 = x0 + s; uint32 z1 = z0 + s;
			uint32 x2 = x0 + s2; uint32 z2 = z0 + s2;
			pushTriGrid(x2, z2, x1, z1, x0, z2);
			pushTriGrid(x2, z2, x1, z1, x2, z0);
			pushTriGrid(x0, z1, x1, z1, x0, z2);
			pushTriGrid(x1, z0, x1, z1, x2, z0);
		}

		// Bottom edge.
		if (!bStitchB)
		{
			const uint32 qxBegin = bStitchL ? 1u : 0u;
			const uint32 qxEnd = bStitchR ? (quadsInThisLod - 1u) : quadsInThisLod;

			for (uint32 qx = qxBegin; qx < qxEnd; ++qx)
			{
				pushQuadStd(qx * step, 0u);
			}
		}
		else
		{
			const uint32 xBegin = bCornerBL ? s2 : 0u;
			const uint32 xEnd = bCornerBR ? (Q - s2) : Q;
			emitStitchStripX(0u, s, xBegin, xEnd);
		}

		// Top edge.
		if (!bStitchT)
		{
			const uint32 qxBegin = bStitchL ? 1u : 0u;
			const uint32 qxEnd = bStitchR ? (quadsInThisLod - 1u) : quadsInThisLod;

			for (uint32 qx = qxBegin; qx < qxEnd; ++qx)
			{
				pushQuadStd(qx * step, Q - step);
			}
		}
		else
		{
			const uint32 xBegin = bCornerTL ? s2 : 0u;
			const uint32 xEnd = bCornerTR ? (Q - s2) : Q;
			emitStitchStripX(Q, Q - s, xBegin, xEnd);
		}

		// Left edge.
		if (quadsInThisLod >= 2u)
		{
			if (!bStitchL)
			{
				for (uint32 qz = 1u; qz < quadsInThisLod - 1u; ++qz)
				{
					pushQuadStd(0u, qz * step);
				}
			}
			else
			{
				const uint32 zBegin = bCornerBL ? s2 : 0u;
				const uint32 zEnd = bCornerTL ? (Q - s2) : Q;
				emitStitchStripZ(0u, s, zBegin, zEnd);
			}
		}

		// Right edge.
		if (quadsInThisLod >= 2u)
		{
			if (!bStitchR)
			{
				for (uint32 qz = 1u; qz < quadsInThisLod - 1u; ++qz)
				{
					pushQuadStd(Q - step, qz * step);
				}
			}
			else
			{
				const uint32 zBegin = bCornerBR ? s2 : 0u;
				const uint32 zEnd = bCornerTR ? (Q - s2) : Q;
				emitStitchStripZ(Q, Q - s, zBegin, zEnd);
			}
		}

		ASSERT((outIdxU16.size() % 3u) == 0u, "Index count must be multiple of 3.");
	}

	bool TerrainSystem::uploadTextureAssetWithMips(
		Renderer& renderer,
		AssetManager& assetManager,
		const char* resourceName,
		const AssetRef<Texture>& texRef,
		const char* shaderStaticName)
	{
		if (!texRef.IsValid())
		{
			return false;
		}

		AssetPtr<Texture> texPtr = assetManager.LoadBlocking(texRef);
		ASSERT(texPtr && texPtr->IsValid(), "Failed to load Texture asset for resource '%s'.", resourceName);

		const auto& mips = texPtr->GetMips();
		ASSERT(!mips.empty(), "TextureAsset '%s' has no mips.", resourceName);

		TextureDesc desc = {};
		desc.Name = resourceName;
		desc.Type = RESOURCE_DIM_TEX_2D;
		desc.Width = mips[0].Width;
		desc.Height = mips[0].Height;
		desc.MipLevels = static_cast<uint32>(mips.size());
		desc.ArraySize = 1;
		desc.Format = texPtr->GetFormat();
		desc.Usage = USAGE_DEFAULT;
		desc.BindFlags = BIND_SHADER_RESOURCE;

		const auto& fmtAttrib = GetTextureFormatAttribs(desc.Format);
		const uint32 elemSize = fmtAttrib.GetElementSize();
		ASSERT(elemSize > 0u, "Invalid element size for texture '%s'.", resourceName);

		std::vector<TextureSubResData> subres;
		subres.resize(mips.size());

		for (size_t i = 0; i < mips.size(); ++i)
		{
			const TextureMip& mip = mips[i];
			ASSERT(mip.Width > 0 && mip.Height > 0, "Invalid mip size for '%s' mip=%zu.", resourceName, i);
			ASSERT(!mip.Data.empty(), "Mip data empty for '%s' mip=%zu.", resourceName, i);

			TextureSubResData sr = {};
			sr.pData = mip.Data.data();
			sr.Stride = static_cast<uint64>(mip.Width) * static_cast<uint64>(elemSize);
			sr.DepthStride = 0;
			subres[i] = sr;
		}

		TextureData initData = {};
		initData.pSubResources = subres.data();
		initData.NumSubresources = static_cast<uint32>(subres.size());

		renderer.AddTexture(STRING_HASH(resourceName), desc, &initData);
		renderer.RegisterStaticTextureResource(shaderStaticName, STRING_HASH(resourceName));
		return true;
	}
} // namespace shz