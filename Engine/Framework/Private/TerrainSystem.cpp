#include "pch.h"
#include "Engine/Framework/Public/TerrainSystem.h"

#include "Engine/AssetManager/Public/AssetManager.h"
#include "Engine/RuntimeData/Public/MaterialManager.h"

#include "Engine/GraphicsUtils/Public/GraphicsUtils.hpp"
#include "Engine/Renderer/Public/Renderer.h"
#include "Engine/Renderer/Public/RenderPassContext.h"
#include "Engine/Renderer/Public/StaticMeshRenderData.h"

namespace shz
{
	// ------------------------------------------------------------
	// Helpers
	// ------------------------------------------------------------
	static inline float u16ToNormalized(uint16 v) noexcept
	{
		return static_cast<float>(v) * (1.0f / 65535.0f);
	}

	static inline uint16 normalizedToU16(float n) noexcept
	{
		const float c = Clamp01(n);
		const float scaled = c * 65535.0f;
		const uint32 iv = static_cast<uint32>(scaled + 0.5f);
		return static_cast<uint16>(iv > 65535u ? 65535u : iv);
	}

	// ------------------------------------------------------------
	// Lifecycle
	// ------------------------------------------------------------
	void TerrainSystem::Initialize(Renderer& renderer, AssetManager& assetManager, const CreateInfo& ci)
	{
		Cleanup();

		m_CI = ci;

		ASSERT(!m_CI.HeightMapPath.empty(), "TerrainSystem HeightMapPath is empty.");
		ASSERT(m_CI.WorldSpacingX > 0.f && m_CI.WorldSpacingZ > 0.f, "Invalid spacing.");
		ASSERT(m_CI.HeightScale >= 0.f, "HeightScale must be >= 0.");

		m_WorldSpacingX = m_CI.WorldSpacingX;
		m_WorldSpacingZ = m_CI.WorldSpacingZ;
		m_HeightScale = m_CI.HeightScale;
		m_HeightOffset = m_CI.HeightOffset;
		m_bCenterXZ = m_CI.bCenterXZ;

		// Height texture (CPU)
		m_HeightTexRef = assetManager.RegisterAsset<Texture>(m_CI.HeightMapPath);
		m_HeightTex = assetManager.LoadBlocking<Texture>(m_HeightTexRef);
		ASSERT(m_HeightTex && m_HeightTex->IsValid(), "Failed to load height Texture asset.");

		// Optional diffuse
		if (!m_CI.DiffusePath.empty())
		{
			m_DiffuseTexRef = assetManager.RegisterAsset<Texture>(m_CI.DiffusePath);
			m_DiffuseTex = assetManager.LoadBlocking<Texture>(m_DiffuseTexRef);
			ASSERT(m_DiffuseTex && m_DiffuseTex->IsValid(), "Failed to load diffuse Texture asset.");
		}

		// Build CPU height samples
		buildHeightU16FromHeightTexture(*m_HeightTex);

		ASSERT(m_Width > 0 && m_Height > 0, "Terrain height texture has invalid dimensions.");
		ASSERT(m_HeightU16.size() == size_t(m_Width) * size_t(m_Height), "Height data size mismatch.");

		// Reset render data
		MaterialId tmId = MaterialManager::GetInstance()->CreateMaterial("TerrainMaterial", "DefaultLit");
		Material& tm = MaterialManager::GetInstance()->GetMaterial(tmId);
		tm.SetFloat4("g_BaseColorFactor", float4(150.f, 200.f, 100.f, 255.f) / 255.f);
		tm.SetFloat3("g_EmissiveFactor", float3(0.f, 0.f, 0.f));
		tm.SetFloat("g_EmissiveIntensity", 0.0f);
		tm.SetFloat("g_RoughnessFactor", 0.85f);
		tm.SetFloat("g_NormalScale", 1.0f);
		tm.SetFloat("g_OcclusionStrength", 1.0f);
		tm.SetFloat("g_AlphaCutoff", 0.5f);
		tm.SetFloat("g_MetallicFactor", 0.0f);
		tm.SetUint("g_MaterialFlags", 0);

		MeshBuildSettings ms = {};
		const bool bOk = BuildStaticMesh(&m_FullMeshCPU, tmId, ms);
		ASSERT(bOk && m_FullMeshCPU.IsValid(), "TerrainSystem: failed to build full CPU mesh.");

		m_pFullMeshRD = &renderer.CreateStaticMeshRenderData(m_FullMeshCPU, STRING_HASH("TerrainSystem.FullMesh"), "TerrainFullMeshRD");
		ASSERT(m_pFullMeshRD != nullptr, "TerrainSystem: failed to create StaticMeshRenderData.");

		m_TerrainTRS = Matrix4x4::Identity();
	}

	void TerrainSystem::Cleanup()
	{
		m_CI = {};

		m_Width = 0;
		m_Height = 0;

		m_WorldSpacingX = 1.0f;
		m_WorldSpacingZ = 1.0f;

		m_HeightScale = 100.0f;
		m_HeightOffset = 0.0f;

		m_bCenterXZ = true;

		m_HeightTexRef = {};
		m_DiffuseTexRef = {};

		m_HeightTex.Reset();
		m_DiffuseTex.Reset();

		m_HeightU16.clear();
		m_HeightU16.shrink_to_fit();

		m_FullMeshCPU.Clear();
		m_pFullMeshRD = nullptr;

		m_TerrainTRS = Matrix4x4::Identity();
	}

	void TerrainSystem::Update(RenderScene& scene, const ViewFamily& view)
	{
		ASSERT(m_pFullMeshRD, "Mesh is not initialized.");
		// TODO: Chunking
		static bool bAdded = false;
		if (!bAdded)
		{
			scene.AddObject(*m_pFullMeshRD, m_TerrainTRS);
			bAdded = true;
		}
	}

	float TerrainSystem::GetWorldOriginX() const noexcept
	{
		return m_bCenterXZ ? (-0.5f * GetWorldSizeX()) : 0.0f;
	}

	float TerrainSystem::GetWorldOriginZ() const noexcept
	{
		return m_bCenterXZ ? (-0.5f * GetWorldSizeZ()) : 0.0f;
	}

	// ------------------------------------------------------------
	// Height access
	// ------------------------------------------------------------
	float TerrainSystem::GetNormalizedHeightAt(uint32 x, uint32 z) const
	{
		ASSERT(x < m_Width, "X out of range.");
		ASSERT(z < m_Height, "Z out of range.");

		const uint32 idx = getIndex(x, z);
		ASSERT(idx < static_cast<uint32>(m_HeightU16.size()), "Index out of range.");
		return u16ToNormalized(m_HeightU16[idx]);
	}

	float TerrainSystem::GetWorldHeightAt(uint32 x, uint32 z) const
	{
		const float n = GetNormalizedHeightAt(x, z);
		return m_HeightOffset + n * m_HeightScale;
	}

	float TerrainSystem::SampleNormalizedHeight(float worldX, float worldZ) const
	{
		ASSERT(m_WorldSpacingX > 0.f && m_WorldSpacingZ > 0.f, "Spacing must be > 0.");

		const float originX = GetWorldOriginX();
		const float originZ = GetWorldOriginZ();

		const float gx = (worldX - originX) / m_WorldSpacingX;
		const float gz = (worldZ - originZ) / m_WorldSpacingZ;

		const float maxX = static_cast<float>(m_Width - 1);
		const float maxZ = static_cast<float>(m_Height - 1);

		const float x = Clamp(gx, 0.f, maxX);
		const float z = Clamp(gz, 0.f, maxZ);

		const uint32 x0 = static_cast<uint32>(std::floor(x));
		const uint32 z0 = static_cast<uint32>(std::floor(z));

		const uint32 x1 = (x0 + 1 < m_Width) ? (x0 + 1) : x0;
		const uint32 z1 = (z0 + 1 < m_Height) ? (z0 + 1) : z0;

		const float tx = x - static_cast<float>(x0);
		const float tz = z - static_cast<float>(z0);

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
		const float n = SampleNormalizedHeight(worldX, worldZ);
		return m_HeightOffset + n * m_HeightScale;
	}

	// ------------------------------------------------------------
	// CPU height build from Texture (base mip)
	// ------------------------------------------------------------
	static inline uint8  readR_U8(const uint8* p) noexcept { return p[0]; }
	static inline uint16 readR_U16(const uint16* p) noexcept { return p[0]; }
	static inline float  readR_F32(const float* p) noexcept { return p[0]; }

	void TerrainSystem::buildHeightU16FromHeightTexture(const Texture& heightTex)
	{
		ASSERT(heightTex.IsValid(), "Height texture is invalid.");
		ASSERT(!heightTex.GetMips().empty(), "Height texture has no mips.");

		const uint32 w = heightTex.GetWidth();
		const uint32 h = heightTex.GetHeight();
		ASSERT(w > 0 && h > 0, "Invalid height texture dimensions.");

		const TEXTURE_FORMAT fmt = heightTex.GetFormat();
		const TextureFormatAttribs& a = GetTextureFormatAttribs(fmt);

		ASSERT(a.NumComponents > 0 && a.ComponentSize > 0, "Invalid format attribs.");
		ASSERT(a.ComponentType != COMPONENT_TYPE_COMPRESSED, "Compressed formats are not supported for heightmap CPU sampling.");

		const uint32 numComps = a.NumComponents;
		const uint32 compSize = a.ComponentSize;
		const uint32 bytesPerPixel = numComps * compSize;

		const TextureMip& mip0 = heightTex.GetMips()[0];
		ASSERT(mip0.Width == w && mip0.Height == h, "Mip0 size mismatch.");
		ASSERT(!mip0.Data.empty(), "Mip0 data empty.");

		const uint64 expectedMinBytes = uint64(w) * uint64(h) * uint64(bytesPerPixel);
		ASSERT(uint64(mip0.Data.size()) >= expectedMinBytes, "Mip0 data is smaller than expected tightly packed size.");

		m_Width = w;
		m_Height = h;
		m_HeightU16.assign(size_t(w) * size_t(h), 0u);

		const uint8* src = mip0.Data.data();
		const uint64 rowStride = uint64(w) * uint64(bytesPerPixel);

		if (compSize == 1)
		{
			const float inv = 1.f / 255.f;
			for (uint32 z = 0; z < h; ++z)
			{
				const uint8* row = src + size_t(z) * size_t(rowStride);
				for (uint32 x = 0; x < w; ++x)
				{
					const uint8* px = row + size_t(x) * bytesPerPixel;
					const uint8 r = readR_U8(px);
					m_HeightU16[getIndex(x, z)] = normalizedToU16(Clamp01(float(r) * inv));
				}
			}
		}
		else if (compSize == 2)
		{
			const float inv = 1.f / 65535.f;
			for (uint32 z = 0; z < h; ++z)
			{
				const uint8* rowBytes = src + size_t(z) * size_t(rowStride);
				for (uint32 x = 0; x < w; ++x)
				{
					const uint16* px = reinterpret_cast<const uint16*>(rowBytes + size_t(x) * bytesPerPixel);
					const uint16 r = readR_U16(px);
					m_HeightU16[getIndex(x, z)] = normalizedToU16(Clamp01(float(r) * inv));
				}
			}
		}
		else if (compSize == 4)
		{
			for (uint32 z = 0; z < h; ++z)
			{
				const uint8* rowBytes = src + size_t(z) * size_t(rowStride);
				for (uint32 x = 0; x < w; ++x)
				{
					const float* px = reinterpret_cast<const float*>(rowBytes + size_t(x) * bytesPerPixel);
					const float r = readR_F32(px);
					m_HeightU16[getIndex(x, z)] = normalizedToU16(Clamp01(r));
				}
			}
		}
		else
		{
			ASSERT(false, "Unsupported component size for height texture.");
		}
	}

	// ------------------------------------------------------------
	// Mesh build (full mesh for now)
	// ------------------------------------------------------------
	static inline uint32 idx2D(uint32 x, uint32 z, uint32 w) noexcept
	{
		return z * w + x;
	}

	float3 TerrainSystem::computeNormalCentralDiff(uint32 x, uint32 z, const MeshBuildSettings& s) const
	{
		const uint32 w = GetWidth();
		const uint32 h = GetHeight();

		const uint32 x0 = (x > 0) ? (x - 1) : x;
		const uint32 x1 = (x + 1 < w) ? (x + 1) : x;

		const uint32 z0 = (z > 0) ? (z - 1) : z;
		const uint32 z1 = (z + 1 < h) ? (z + 1) : z;

		const float hl = GetWorldHeightAt(x0, z);
		const float hr = GetWorldHeightAt(x1, z);
		const float hd = GetWorldHeightAt(x, z0);
		const float hu = GetWorldHeightAt(x, z1);

		const float dx = (hr - hl);
		const float dz = (hu - hd);

		const float up = std::max(0.001f, s.NormalUpBias);
		return float3{ -dx, up, -dz }.Normalized();
	}

	bool TerrainSystem::BuildStaticMesh(
		StaticMesh* pOutMesh,
		MaterialId terrainMaterial,
		const MeshBuildSettings& settings) const
	{
		ASSERT(pOutMesh != nullptr, "pOutMesh is null.");

		const uint32 w = GetWidth();
		const uint32 h = GetHeight();
		ASSERT(w >= 4 && h >= 4, "Heightmap resolution is too small.");

		pOutMesh->Clear();

		const uint64 numVertices64 = uint64(w) * uint64(h);
		if (numVertices64 > uint64(std::numeric_limits<uint32>::max()))
		{
			ASSERT(false, "Too many vertices.");
			return false;
		}

		const uint32 numVertices = uint32(numVertices64);

		std::vector<float3> positions;
		std::vector<float3> normals;
		std::vector<float2> uvs;

		positions.resize(numVertices);

		if (settings.bGenerateNormals) normals.resize(numVertices);
		if (settings.bGenerateTexCoords) uvs.resize(numVertices);

		const float spacingX = GetWorldSpacingX();
		const float spacingZ = GetWorldSpacingZ();

		const float sizeX = GetWorldSizeX();
		const float sizeZ = GetWorldSizeZ();

		const float originX = m_bCenterXZ ? (-0.5f * sizeX) : 0.f;
		const float originZ = m_bCenterXZ ? (-0.5f * sizeZ) : 0.f;

		for (uint32 zz = 0; zz < h; ++zz)
		{
			for (uint32 xx = 0; xx < w; ++xx)
			{
				const uint32 i = idx2D(xx, zz, w);

				const float wx = originX + float(xx) * spacingX;
				const float wz = originZ + float(zz) * spacingZ;
				const float wy = GetWorldHeightAt(xx, zz); // world meters already

				positions[i] = float3{ wx, wy, wz };

				if (settings.bGenerateNormals)
				{
					normals[i] = computeNormalCentralDiff(xx, zz, settings);
				}

				if (settings.bGenerateTexCoords)
				{
					const float u = (w > 1) ? (float(xx) / float(w - 1)) : 0.f;
					const float v = (h > 1) ? (float(zz) / float(h - 1)) : 0.f;
					uvs[i] = float2{ Clamp01(u), Clamp01(v) };
				}
			}
		}

		pOutMesh->SetPositions(std::move(positions));
		if (settings.bGenerateNormals)   pOutMesh->SetNormals(std::move(normals));
		if (settings.bGenerateTexCoords) pOutMesh->SetTexCoords(std::move(uvs));

		const uint64 numQuads64 = uint64(w - 1) * uint64(h - 1);
		const uint64 numIndices64 = numQuads64 * 6ull;

		if (numIndices64 > uint64(std::numeric_limits<uint32>::max()))
		{
			ASSERT(false, "Too many indices.");
			return false;
		}

		const uint32 numIndices = uint32(numIndices64);

		const bool bCanUseU16 = (numVertices <= 65535u);
		const bool bUseU16 = settings.bPreferU16Indices && bCanUseU16;

		if (bUseU16)
		{
			std::vector<uint16> indices;
			indices.resize(numIndices);

			uint32 out = 0;
			for (uint32 zz = 0; zz < h - 1; ++zz)
			{
				for (uint32 xx = 0; xx < w - 1; ++xx)
				{
					const uint32 i0 = idx2D(xx, zz, w);
					const uint32 i1 = idx2D(xx + 1, zz, w);
					const uint32 i2 = idx2D(xx, zz + 1, w);
					const uint32 i3 = idx2D(xx + 1, zz + 1, w);

					if (!settings.bFlipWinding)
					{
						indices[out++] = uint16(i0);
						indices[out++] = uint16(i2);
						indices[out++] = uint16(i1);

						indices[out++] = uint16(i1);
						indices[out++] = uint16(i2);
						indices[out++] = uint16(i3);
					}
					else
					{
						indices[out++] = uint16(i0);
						indices[out++] = uint16(i1);
						indices[out++] = uint16(i2);

						indices[out++] = uint16(i1);
						indices[out++] = uint16(i3);
						indices[out++] = uint16(i2);
					}
				}
			}
			pOutMesh->SetIndicesU16(std::move(indices));
		}
		else
		{
			std::vector<uint32> indices;
			indices.resize(numIndices);

			uint32 out = 0;
			for (uint32 zz = 0; zz < h - 1; ++zz)
			{
				for (uint32 xx = 0; xx < w - 1; ++xx)
				{
					const uint32 i0 = idx2D(xx, zz, w);
					const uint32 i1 = idx2D(xx + 1, zz, w);
					const uint32 i2 = idx2D(xx, zz + 1, w);
					const uint32 i3 = idx2D(xx + 1, zz + 1, w);

					if (!settings.bFlipWinding)
					{
						indices[out++] = i0;
						indices[out++] = i1;
						indices[out++] = i2;

						indices[out++] = i1;
						indices[out++] = i3;
						indices[out++] = i2;
					}
					else
					{
						indices[out++] = i0;
						indices[out++] = i2;
						indices[out++] = i1;

						indices[out++] = i1;
						indices[out++] = i2;
						indices[out++] = i3;
					}
				}
			}
			pOutMesh->SetIndicesU32(std::move(indices));
		}

		StaticMesh::Section section = {};
		section.FirstIndex = 0;
		section.IndexCount = pOutMesh->GetIndexCount();
		section.BaseVertex = 0;
		section.MaterialSlot = 0;

		std::vector<StaticMesh::Section> sections;
		sections.emplace_back(static_cast<StaticMesh::Section&&>(section));
		pOutMesh->SetSections(std::move(sections));

		std::vector<MaterialId> materials;
		materials.emplace_back(terrainMaterial);
		pOutMesh->SetMaterialSlots(std::move(materials));

		pOutMesh->RecomputeBounds();

		if (!pOutMesh->IsValid())
		{
			ASSERT(false, "Built terrain StaticMesh is invalid.");
			return false;
		}

		return true;
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
} // namespace shz
