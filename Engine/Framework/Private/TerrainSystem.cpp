#include "pch.h"
#include "Engine/Framework/Public/TerrainSystem.h"

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
		float3 Pos; // x,z in [0..16]
		float2 UV;  // [0..1]
	};

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

	static void buildGrid17x17VB(std::vector<TerrainVertex>& outVerts)
	{
		outVerts.clear();
		outVerts.reserve(17 * 17);

		for (uint32 z = 0; z < 17; ++z)
		{
			for (uint32 x = 0; x < 17; ++x)
			{
				const float u = float(x) / 16.0f;
				const float v = float(z) / 16.0f;

				TerrainVertex vtx = {};
				vtx.Pos = float3{ float(x), 0.0f, float(z) };
				vtx.UV = float2{ u, v };
				outVerts.emplace_back(vtx);
			}
		}
	}

	static void buildGridIndicesLOD(uint32 step, std::vector<uint16>& outIdxU16)
	{
		outIdxU16.clear();

		const uint32 quadsPerSide = 16 / step;
		const uint32 vertsPerSide = 17;

		outIdxU16.reserve(quadsPerSide * quadsPerSide * 6);

		auto vid = [&](uint32 gx, uint32 gz) -> uint16
		{
			return uint16(gz * vertsPerSide + gx);
		};

		for (uint32 qz = 0; qz < quadsPerSide; ++qz)
		{
			for (uint32 qx = 0; qx < quadsPerSide; ++qx)
			{
				const uint32 x0 = qx * step;
				const uint32 z0 = qz * step;
				const uint32 x1 = x0 + step;
				const uint32 z1 = z0 + step;

				const uint16 i0 = vid(x0, z0);
				const uint16 i1 = vid(x1, z0);
				const uint16 i2 = vid(x0, z1);
				const uint16 i3 = vid(x1, z1);

				outIdxU16.push_back(i0);
				outIdxU16.push_back(i1);
				outIdxU16.push_back(i2);

				outIdxU16.push_back(i1);
				outIdxU16.push_back(i3);
				outIdxU16.push_back(i2);
			}
		}
	}

	// ------------------------------------------------------------
	// Lifecycle
	// ------------------------------------------------------------
	void TerrainSystem::Initialize(AssetManager& assetManager, const CreateInfo& ci)
	{
		Cleanup();
		m_CI = ci;

		ASSERT(!m_CI.HeightMapPath.empty(), "TerrainSystem HeightMapPath is empty.");
		ASSERT(m_CI.WorldSpacingX > 0.f && m_CI.WorldSpacingZ > 0.f, "Invalid spacing.");
		ASSERT(m_CI.HeightScale >= 0.f, "HeightScale must be >= 0.");

		m_ChunkSize = m_CI.ChunkSize;
		m_WorldSpacingX = m_CI.WorldSpacingX;
		m_WorldSpacingZ = m_CI.WorldSpacingZ;
		m_HeightScale = m_CI.HeightScale;
		m_HeightOffset = m_CI.HeightOffset;
		m_bCenterXZ = m_CI.bCenterXZ;

		// Height texture (CPU)
		m_HeightTexRef = assetManager.RegisterAsset<Texture>(m_CI.HeightMapPath);
		m_HeightTex = assetManager.LoadBlocking<Texture>(m_HeightTexRef);
		ASSERT(m_HeightTex && m_HeightTex->IsValid(), "Failed to load height Texture asset.");

		// Optional diffuse (CPU)
		if (!m_CI.DiffusePath.empty())
		{
			m_DiffuseTexRef = assetManager.RegisterAsset<Texture>(m_CI.DiffusePath);
			m_DiffuseTex = assetManager.LoadBlocking<Texture>(m_DiffuseTexRef);
			ASSERT(m_DiffuseTex && m_DiffuseTex->IsValid(), "Failed to load diffuse Texture asset.");
		}

		// CPU height array
		buildHeightU16FromHeightTexture(*m_HeightTex);
		ASSERT(m_Width > 0 && m_Height > 0, "Terrain height texture has invalid dimensions.");
		ASSERT(m_HeightU16.size() == size_t(m_Width) * size_t(m_Height), "Height data size mismatch.");
	}

	void TerrainSystem::Cleanup()
	{
		m_CI = {};

		m_Width = 0;
		m_Height = 0;

		m_ChunkSize = 64.0f;
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

		m_pTerrainGBufferPSO.Release();
		m_pTerrainGBufferSRB.Release();

		m_pGridVB.Release();
		for (auto& ib : m_pLodIB) ib.Release();

		for (uint32& c : m_LodIndexCount) c = 0;
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
	// Pass registration (feature-owned)
	// ------------------------------------------------------------
	void TerrainSystem::InstallPasses(Renderer& renderer)
	{
		// TerrainGBuffer: clears + draws terrain into same GBuffer targets
		renderer.AddPass(
			"TerrainGBuffer",
			[](RenderPassBuilder& b)
			{
				const uint64 kAlbedo = STRING_HASH("GBuffer0_Albedo");
				const uint64 kNormal = STRING_HASH("GBuffer1_Normal");
				const uint64 kMRAO = STRING_HASH("GBuffer2_MRAO");
				const uint64 kEmissive = STRING_HASH("GBuffer3_Emissive");
				const uint64 kDepth = STRING_HASH("GBufferDepth");

				b.DeclareTextureRTVWrite(kAlbedo);
				b.DeclareTextureRTVWrite(kNormal);
				b.DeclareTextureRTVWrite(kMRAO);
				b.DeclareTextureRTVWrite(kEmissive);
				b.DeclareTextureDSVWrite(kDepth);
				b.DeclareTextureSRVRead(STRING_HASH("HeightField"));

				b.SetClearColor(kAlbedo, 0.f, 0.f, 0.f, 0.f);
				b.SetClearColor(kNormal, 0.f, 0.f, 0.f, 0.f);
				b.SetClearColor(kMRAO, 0.f, 0.f, 0.f, 0.f);
				b.SetClearColor(kEmissive, 0.f, 0.f, 0.f, 0.f);
				b.SetClearDepthStencil(kDepth, 1.f, 0);
			},
			[this](RenderPassContext& ctx)
			{
				ASSERT(ctx.pImmediateContext, "Context is null.");
				ASSERT(ctx.pRegistry, "Registry is null.");
				ASSERT(m_pTerrainGBufferPSO && m_pTerrainGBufferSRB, "Terrain PSO/SRB not initialized.");

				IDeviceContext* pCtx = ctx.pImmediateContext;

				// TEMP: whole terrain as LOD0
				const uint32 lod = 0;

				const float worldOriginX = GetWorldOriginX();
				const float worldOriginZ = GetWorldOriginZ();
				const float worldSizeX = GetWorldSizeX();
				const float worldSizeZ = GetWorldSizeZ();

				const uint32 numChunksX = uint32(std::ceil(worldSizeX / m_ChunkSize));
				const uint32 numChunksZ = uint32(std::ceil(worldSizeZ / m_ChunkSize));

				pCtx->SetPipelineState(m_pTerrainGBufferPSO);
				pCtx->CommitShaderResources(m_pTerrainGBufferSRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);

				// VB/IB
				{
					IBuffer* vbs[] = { m_pGridVB };
					uint64 offs[] = { 0 };

					pCtx->SetVertexBuffers(
						0, 1, vbs, offs,
						RESOURCE_STATE_TRANSITION_MODE_VERIFY,
						SET_VERTEX_BUFFERS_FLAG_RESET);

					pCtx->SetIndexBuffer(m_pLodIB[lod], 0, RESOURCE_STATE_TRANSITION_MODE_VERIFY);
				}

				DrawIndexedAttribs dia = {};
				dia.IndexType = VT_UINT16;
				dia.NumIndices = m_LodIndexCount[lod];
				dia.Flags = DRAW_FLAG_VERIFY_ALL;

				for (uint32 cz = 0; cz < numChunksZ; ++cz)
				{
					for (uint32 cx = 0; cx < numChunksX; ++cx)
					{
						const float chunkOriginX = worldOriginX + float(cx) * m_ChunkSize;
						const float chunkOriginZ = worldOriginZ + float(cz) * m_ChunkSize;

						const float remainX = worldOriginX + worldSizeX - chunkOriginX;
						const float remainZ = worldOriginZ + worldSizeZ - chunkOriginZ;

						const float chunkSizeX = (remainX > 0.f) ? Min(m_ChunkSize, remainX) : 0.f;
						const float chunkSizeZ = (remainZ > 0.f) ? Min(m_ChunkSize, remainZ) : 0.f;

						if (chunkSizeX <= 1e-6f || chunkSizeZ <= 1e-6f)
							continue;

						// (선택) 간단한 청크 프러스텀 컬링 넣고 싶으면 여기서 AABB 체크

						hlsl::TerrainDrawConstants dc = {};
						dc.ChunkOriginXZ = float2{ chunkOriginX, chunkOriginZ };
						dc.ChunkSizeXZ = float2{ chunkSizeX,   chunkSizeZ };

						dc.HeightUVScale = float2{ 1, 1 };
						dc.HeightUVBias = float2{ 0, 0 };
						dc.SurfaceUVScale = float2{ 1, 1 };
						dc.SurfaceUVBias = float2{ 0, 0 };
						dc.NormalSampleStep = 1.0f;

						auto hash01 = [](uint32 v) -> float
						{
							v ^= v >> 16; v *= 0x7feb352d; v ^= v >> 15; v *= 0x846ca68b; v ^= v >> 16; return float(v & 0x00FFFFFFu) / 16777216.0f;
						};

						uint32 h = (cx + 1) * 73856093u ^ (cz + 1) * 19349663u;
						float r = 0.25f + 0.75f * hash01(h ^ 0x1111u);
						float g = 0.25f + 0.75f * hash01(h ^ 0x2222u);
						float b = 0.25f + 0.75f * hash01(h ^ 0x3333u);

						dc.DebugChunkColor = float4{ r, g, b, 1.0f };

						MapHelper<hlsl::TerrainDrawConstants> map(
							pCtx,
							ctx.pRenderer->GetBuffer(STRING_HASH("TerrainDrawConstants")),
							MAP_WRITE,
							MAP_FLAG_DISCARD);

						*map = dc;

						pCtx->DrawIndexed(dia);
					}
				}
			},
				[this, &renderer]()
			{
				// VB
				{
					std::vector<TerrainVertex> verts;
					buildGrid17x17VB(verts);

					BufferDesc vb = {};
					vb.Name = "Terrain.Grid17x17.VB";
					vb.Usage = USAGE_IMMUTABLE;
					vb.BindFlags = BIND_VERTEX_BUFFER;
					vb.Size = uint32(verts.size() * sizeof(TerrainVertex));

					BufferData init = {};
					init.pData = verts.data();
					init.DataSize = vb.Size;

					m_pGridVB = renderer.CreateVertexBuffer(vb, &init);
					ASSERT(m_pGridVB, "Create terrain VB failed.");
				}

				// IBs
				const uint32 steps[5] = { 1, 2, 4, 8, 16 };
				for (uint32 i = 0; i < 5; ++i)
				{
					std::vector<uint16> idx;
					buildGridIndicesLOD(steps[i], idx);

					m_LodIndexCount[i] = uint32(idx.size());

					BufferDesc ib = {};
					std::string name = "Terrain.Grid17x17.IB.step" + std::to_string(steps[i]);
					ib.Name = name.c_str();
					ib.Usage = USAGE_IMMUTABLE;
					ib.BindFlags = BIND_INDEX_BUFFER;
					ib.Size = uint32(idx.size() * sizeof(uint16));

					BufferData init = {};
					init.pData = idx.data();
					init.DataSize = ib.Size;

					m_pLodIB[i] = renderer.CreateIndexBuffer(ib, &init);
					ASSERT(m_pLodIB[i], "Create terrain IB failed.");
				}

				{
					BufferDesc cb = {};
					cb.Name = "TERRAIN_DRAW_CONSTANTS";
					cb.Usage = USAGE_DYNAMIC;
					cb.BindFlags = BIND_UNIFORM_BUFFER;
					cb.CPUAccessFlags = CPU_ACCESS_WRITE;
					cb.Size = sizeof(hlsl::TerrainDrawConstants);
					renderer.AddBuffer("TerrainDrawConstants", cb);
				}

				{
					GraphicsPipelineStateCreateInfo psoCi = {};
					psoCi.PSODesc.Name = "TerrainGBuffer PSO";
					psoCi.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

					GraphicsPipelineDesc& gp = psoCi.GraphicsPipeline;
					gp.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
					gp.RasterizerDesc.CullMode = CULL_MODE_BACK;
					gp.RasterizerDesc.FrontCounterClockwise = true;

					// TEST
					gp.RasterizerDesc.CullMode = CULL_MODE_NONE;
					gp.RasterizerDesc.FillMode = FILL_MODE_WIREFRAME;

					gp.DepthStencilDesc.DepthEnable = true;
					gp.DepthStencilDesc.DepthWriteEnable = true;
					gp.DepthStencilDesc.DepthFunc = COMPARISON_FUNC_LESS_EQUAL;

					LayoutElement elems[] =
					{
						LayoutElement{ 0, 0, 3, VT_FLOAT32, false }, // ATTRIB0 Pos
						LayoutElement{ 1, 0, 2, VT_FLOAT32, false }, // ATTRIB1 UV
					};
					gp.InputLayout.LayoutElements = elems;
					gp.InputLayout.NumElements = _countof(elems);

					ShaderCreateInfo vsCI = {};
					vsCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
					vsCI.EntryPoint = "main";
					vsCI.Desc.Name = "Terrain VS";
					vsCI.Desc.ShaderType = SHADER_TYPE_VERTEX;
					vsCI.FilePath = m_TerrainVS.c_str();

					ShaderCreateInfo psCI = {};
					psCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
					psCI.EntryPoint = "main";
					psCI.Desc.Name = "Terrain PS (reuse GBuffer)";
					psCI.Desc.ShaderType = SHADER_TYPE_PIXEL;
					psCI.FilePath = m_TerrainPS.c_str(); // GBuffer.psh

					renderer.CreateShader(vsCI, &psoCi.pVS);
					renderer.CreateShader(psCI, &psoCi.pPS);

					psoCi.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

					ShaderResourceVariableDesc vars[] =
					{
						// VS
						{ SHADER_TYPE_VERTEX, "TERRAIN_DRAW_CONSTANTS", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },

						{ SHADER_TYPE_PIXEL, "MATERIAL_CONSTANTS", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
						{ SHADER_TYPE_PIXEL, "g_BaseColorTex", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
						{ SHADER_TYPE_PIXEL, "g_NormalTex", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
						{ SHADER_TYPE_PIXEL, "g_MetallicRoughnessTex", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
						{ SHADER_TYPE_PIXEL, "g_AOTex", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
						{ SHADER_TYPE_PIXEL, "g_EmissiveTex", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
						{ SHADER_TYPE_PIXEL, "g_HeightTex", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					};

					psoCi.PSODesc.ResourceLayout.Variables = vars;
					psoCi.PSODesc.ResourceLayout.NumVariables = _countof(vars);

					SamplerDesc linearClamp =
					{
						FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR,
						TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP
					};

					SamplerDesc linearWrap =
					{
						FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR,
						TEXTURE_ADDRESS_WRAP, TEXTURE_ADDRESS_WRAP, TEXTURE_ADDRESS_WRAP
					};

					ImmutableSamplerDesc samplers[] =
					{
						{ SHADER_TYPE_VERTEX, "g_LinearClampSampler", linearClamp },
						{ SHADER_TYPE_PIXEL, "g_LinearWrapSampler", linearWrap },
					};
					psoCi.PSODesc.ResourceLayout.ImmutableSamplers = samplers;
					psoCi.PSODesc.ResourceLayout.NumImmutableSamplers = _countof(samplers);

					// Pass-compatible PSO (important)
					m_pTerrainGBufferPSO = renderer.AcquirePipelineState(STRING_HASH("TerrainGBuffer"), psoCi);
					ASSERT(m_pTerrainGBufferPSO, "AcquirePipelineState(TerrainGBuffer) failed.");

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

					m_pTerrainGBufferSRB = renderer.AcquireShaderResourceBindingFromMaterial(tmId, m_pTerrainGBufferPSO);

					// Bind constant buffers
					if (auto* v = m_pTerrainGBufferSRB->GetVariableByName(SHADER_TYPE_VERTEX, "TERRAIN_DRAW_CONSTANTS"))
					{
						v->Set(renderer.GetBuffer(STRING_HASH("TerrainDrawConstants")), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
					}
				}
			});
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

		if (a.ComponentSize == 1)
		{
			const float inv = 1.f / 255.f;
			for (uint32 z = 0; z < h; ++z)
			{
				const uint8* row = src + size_t(z) * size_t(rowStride);
				for (uint32 x = 0; x < w; ++x)
				{
					const uint8* px = row + size_t(x) * bytesPerPixel;
					const uint8 r = readR_U8(px);
					m_HeightU16[size_t(z) * size_t(w) + x] = normalizedToU16(Clamp01(float(r) * inv));
				}
			}
		}
		else if (a.ComponentSize == 2)
		{
			const float inv = 1.f / 65535.f;
			for (uint32 z = 0; z < h; ++z)
			{
				const uint8* rowBytes = src + size_t(z) * size_t(rowStride);
				for (uint32 x = 0; x < w; ++x)
				{
					const uint16* px = reinterpret_cast<const uint16*>(rowBytes + size_t(x) * bytesPerPixel);
					const uint16 r = readR_U16(px);
					m_HeightU16[size_t(z) * size_t(w) + x] = normalizedToU16(Clamp01(float(r) * inv));
				}
			}
		}
		else if (a.ComponentSize == 4)
		{
			for (uint32 z = 0; z < h; ++z)
			{
				const uint8* rowBytes = src + size_t(z) * size_t(rowStride);
				for (uint32 x = 0; x < w; ++x)
				{
					const float* px = reinterpret_cast<const float*>(rowBytes + size_t(x) * bytesPerPixel);
					const float r = readR_F32(px);
					m_HeightU16[size_t(z) * size_t(w) + x] = normalizedToU16(Clamp01(r));
				}
			}
		}
		else
		{
			ASSERT(false, "Unsupported component size.");
		}
	}

	// ------------------------------------------------------------
	// Height access
	// ------------------------------------------------------------
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
		ASSERT(m_WorldSpacingX > 0.f && m_WorldSpacingZ > 0.f, "Spacing must be > 0.");

		const float originX = GetWorldOriginX();
		const float originZ = GetWorldOriginZ();

		const float gx = (worldX - originX) / m_WorldSpacingX;
		const float gz = (worldZ - originZ) / m_WorldSpacingZ;

		const float maxX = float(m_Width - 1);
		const float maxZ = float(m_Height - 1);

		const float x = Clamp(gx, 0.f, maxX);
		const float z = Clamp(gz, 0.f, maxZ);

		const uint32 x0 = uint32(std::floor(x));
		const uint32 z0 = uint32(std::floor(z));

		const uint32 x1 = (x0 + 1 < m_Width) ? (x0 + 1) : x0;
		const uint32 z1 = (z0 + 1 < m_Height) ? (z0 + 1) : z0;

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
} // namespace shz
