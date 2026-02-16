#include "pch.h"
#include "Engine/RenderSystem/Public/GrassSystem.h"

#include <cstring>

#include "Engine/Renderer/Public/Renderer.h"
#include "Engine/Renderer/Public/RenderPassContext.h"
#include "Engine/Renderer/Public/RenderPassBuilder.h"
#include "Engine/Renderer/Public/RenderResourceRegistry.h"
#include "Engine/Renderer/Public/RenderScene.h"
#include "Engine/Renderer/Public/StaticMeshRenderData.h"

#include "Engine/RenderSystem/Public/IndirectArgsSystem.h"
#include "Engine/RenderSystem/Public/InteractionSystem.h"

#include "Engine/GraphicsTools/Public/MapHelper.hpp"

namespace shz
{
	namespace hlsl
	{
#include "Shaders/HLSL_Structures.hlsli"
	}

	static inline uint32 DivUp(uint32 x, uint32 d) { return (x + d - 1u) / d; }

	void GrassSystem::ClearGrassDescs()
	{
		m_GrassDescs.clear();
		m_SpeciesIndirect.clear();
	}

	uint32 GrassSystem::AddGrassDesc(const GrassDesc& desc)
	{
		ASSERT(m_GrassDescs.size() < MAX_GRASS_SPECIES, "Max grass species exceeded.");
		ASSERT(desc.pMesh, "Mesh is null");

		m_GrassDescs.push_back(desc);

		SpeciesIndirect si = {};
		m_SpeciesIndirect.push_back(si);

		return (uint32)(m_GrassDescs.size() - 1u);
	}

	void GrassSystem::InstallPasses(
		Renderer& renderer,
		RenderScene& scene,
		IndirectArgsSystem& indirect,
		const InteractionSystem& interaction)
	{
		m_pInteractionSystem = &interaction;

		ASSERT(!m_GrassDescs.empty(), "No grass species. Call AddGrassDesc().");
		ASSERT(m_GrassDescs.size() == m_SpeciesIndirect.size(), "Internal species arrays mismatch.");

		const uint32 visibleDim = 2u * m_ChunkHalfExtent;
		const uint32 visibleCells = visibleDim * visibleDim;
		const uint32 numPools = visibleCells;

		const uint32 numSpecies = (uint32)m_GrassDescs.size();
		ASSERT(numSpecies <= MAX_GRASS_SPECIES, "Max grass species exceeded.");

		// ---------------------------------------------------------------------
		// Buffers (render instances) - shared per LOD, packed by species
		// ---------------------------------------------------------------------
		{
			BufferDesc bd = {};
			bd.Usage = USAGE_DEFAULT;
			bd.BindFlags = BIND_SHADER_RESOURCE | BIND_UNORDERED_ACCESS;
			bd.Mode = BUFFER_MODE_STRUCTURED;

			bd.Name = "GrassInstanceBufferLOD0";
			bd.ElementByteStride = sizeof(hlsl::GrassMeshInstance);
			bd.Size = MAX_NUM_GRASS_LOD0_INSTANCES * sizeof(hlsl::GrassMeshInstance);
			renderer.AddBuffer(STRING_HASH("GrassInstanceBufferLOD0"), bd);

			bd.Name = "GrassInstanceBufferLOD1";
			bd.ElementByteStride = sizeof(hlsl::GrassCrossPlaneInstance);
			bd.Size = MAX_NUM_GRASS_LOD1_INSTANCES * sizeof(hlsl::GrassCrossPlaneInstance);
			renderer.AddBuffer(STRING_HASH("GrassInstanceBufferLOD1"), bd);

			bd.Name = "GrassInstanceBufferLOD2";
			bd.ElementByteStride = sizeof(hlsl::GrassBillboardInstance);
			bd.Size = MAX_NUM_GRASS_LOD2_INSTANCES * sizeof(hlsl::GrassBillboardInstance);
			renderer.AddBuffer(STRING_HASH("GrassInstanceBufferLOD2"), bd);
		}

		// ---------------------------------------------------------------------
		// ChunkPool buffers (poolIndex == cellIndex)
		// ---------------------------------------------------------------------
		{
			BufferDesc bd = {};
			bd.Usage = USAGE_DEFAULT;
			bd.BindFlags = BIND_SHADER_RESOURCE | BIND_UNORDERED_ACCESS;
			bd.Mode = BUFFER_MODE_STRUCTURED;

			bd.ElementByteStride = 16;
			bd.Size = uint64(visibleCells) * 16ull;
			bd.Name = "Grass_VisibleCellTable";
			renderer.AddBuffer(STRING_HASH("Grass_VisibleCellTable"), bd);

			bd.ElementByteStride = 16;
			bd.Size = uint64(numPools) * 16ull;
			bd.Name = "Grass_PoolChunkCoord";
			renderer.AddBuffer(STRING_HASH("Grass_PoolChunkCoord"), bd);

			bd.ElementByteStride = 16;
			bd.Size = uint64(numPools) * 16ull;
			bd.Name = "Grass_PoolDirty";
			renderer.AddBuffer(STRING_HASH("Grass_PoolDirty"), bd);

			bd.ElementByteStride = 16;
			bd.Size = uint64(numPools) * uint64(m_SamplesPerChunk) * 16ull;
			bd.Name = "Grass_PoolPositions";
			renderer.AddBuffer(STRING_HASH("Grass_PoolPositions"), bd);
		}

		// ---------------------------------------------------------------------
		// Species packing buffers
		// - "Grass_SpeciesLodCounts" (YOU will bind)
		// - "Grass_SpeciesLodOffsets" (YOU will bind)
		// - "Grass_SpeciesLodWriteCounters" (internal)
		// ---------------------------------------------------------------------
		{
			BufferDesc bd = {};
			bd.Usage = USAGE_DEFAULT;
			bd.BindFlags = BIND_SHADER_RESOURCE | BIND_UNORDERED_ACCESS;
			bd.Mode = BUFFER_MODE_STRUCTURED;
			bd.ElementByteStride = 4;

			const uint64 numElems = uint64(MAX_GRASS_SPECIES) * 3ull;
			bd.Size = numElems * 4ull;

			bd.Name = "Grass_SpeciesLodCounts";
			renderer.AddBuffer(STRING_HASH("Grass_SpeciesLodCounts"), bd);

			bd.Name = "Grass_SpeciesLodOffsets";
			renderer.AddBuffer(STRING_HASH("Grass_SpeciesLodOffsets"), bd);

			bd.Name = "Grass_SpeciesLodWriteCounters";
			renderer.AddBuffer(STRING_HASH("Grass_SpeciesLodWriteCounters"), bd);
		}

		// Species -> MeshId lookup tables (SRV only)
		{
			BufferDesc bd = {};
			bd.Usage = USAGE_DEFAULT;
			bd.BindFlags = BIND_SHADER_RESOURCE;
			bd.Mode = BUFFER_MODE_STRUCTURED;

			bd.ElementByteStride = sizeof(uint32);
			bd.Size = uint64(MAX_GRASS_SPECIES) * sizeof(uint32);

			bd.Name = "Grass_SpeciesLOD0MeshId";
			renderer.AddBuffer(STRING_HASH("Grass_SpeciesLOD0MeshId"), bd);

			bd.Name = "Grass_SpeciesLOD1MeshId";
			renderer.AddBuffer(STRING_HASH("Grass_SpeciesLOD1MeshId"), bd);

			bd.Name = "Grass_SpeciesLOD2MeshId";
			renderer.AddBuffer(STRING_HASH("Grass_SpeciesLOD2MeshId"), bd);
		}

		// ---------------------------------------------------------------------
		// Constants buffer (CS)
		// ---------------------------------------------------------------------
		{
			BufferDesc bd = {};
			bd.Name = "GrassGenConstants";
			bd.Usage = USAGE_DYNAMIC;
			bd.BindFlags = BIND_UNIFORM_BUFFER;
			bd.CPUAccessFlags = CPU_ACCESS_WRITE;
			bd.Size = sizeof(hlsl::GrassGenConstants);
			renderer.AddBuffer(STRING_HASH("GrassGenConstants"), bd);
		}

		// ---------------------------------------------------------------------
		// Allocate indirect mesh ranges per species + per LOD
		// ---------------------------------------------------------------------
		for (uint32 sp = 0u; sp < numSpecies; ++sp)
		{
			const GrassDesc& gd = m_GrassDescs[sp];

			const uint32 lod0Sections = (uint32)gd.pMesh->GetLevel(0).Sections.size();
			const uint32 lod1Sections = (uint32)gd.pMesh->GetLevel(1).Sections.size();
			const uint32 lod2Sections = (uint32)gd.pMesh->GetLevel(2).Sections.size();

			m_SpeciesIndirect[sp].LOD0 = indirect.AllocateMesh("GrassLOD0_Species", lod0Sections);
			m_SpeciesIndirect[sp].LOD1 = indirect.AllocateMesh("GrassLOD1_Species", lod1Sections);
			m_SpeciesIndirect[sp].LOD2 = indirect.AllocateMesh("GrassLOD2_Species", lod2Sections);

			auto SetMeshTemplates = [&](const StaticMeshLevelRenderData& mesh, uint32 baseSlot)
			{
				const uint32 sectionCount = (uint32)mesh.Sections.size();
				for (uint32 si = 0u; si < sectionCount; ++si)
				{
					const auto& sec = mesh.Sections[si];

					hlsl::IndirectArgsTemplate t = {};
					t.IndexCountPerInstance = sec.IndexCount;
					t.StartIndexLocation = sec.FirstIndex;
					t.BaseVertexLocation = sec.BaseVertex;
					t.StartInstanceLocation = 0;

					indirect.SetTemplate(baseSlot + si, t);
				}
			};

			SetMeshTemplates(gd.pMesh->GetLevel(0), m_SpeciesIndirect[sp].LOD0.BaseSlot);
			SetMeshTemplates(gd.pMesh->GetLevel(1), m_SpeciesIndirect[sp].LOD1.BaseSlot);
			SetMeshTemplates(gd.pMesh->GetLevel(2), m_SpeciesIndirect[sp].LOD2.BaseSlot);

			// Register indirect objects to RenderScene (one per species per LOD)
			{
				RenderScene::IndirectObjectDesc d = {};
				d.bCastShadow = true;
				d.PassKey = STRING_HASH("GBuffer");

				// LOD0
				d.bDepthPrepass = false;
				d.pMesh = &gd.pMesh->GetLevel(0);
				d.IndirectBaseSlot = m_SpeciesIndirect[sp].LOD0.BaseSlot;
				d.IndirectMeshId = m_SpeciesIndirect[sp].LOD0.MeshId;
				d.StartInstanceLocation = sp * 3u + 0u;
				scene.AddIndirect(d);

				// LOD1
				d.bDepthPrepass = false;
				d.pMesh = &gd.pMesh->GetLevel(1);
				d.IndirectBaseSlot = m_SpeciesIndirect[sp].LOD1.BaseSlot;
				d.IndirectMeshId = m_SpeciesIndirect[sp].LOD1.MeshId;
				d.StartInstanceLocation = sp * 3u + 1u;
				scene.AddIndirect(d);

				// LOD2
				d.bDepthPrepass = false;
				d.pMesh = &gd.pMesh->GetLevel(2);
				d.IndirectBaseSlot = m_SpeciesIndirect[sp].LOD2.BaseSlot;
				d.IndirectMeshId = m_SpeciesIndirect[sp].LOD2.MeshId;
				d.StartInstanceLocation = sp * 3u + 2u;
				scene.AddIndirect(d);
			}
		}


		auto uploadSpeciesMeshIdTable = [&](uint64 bufferId, const std::vector<uint32>& meshIds)
		{
			std::vector<uint8> bytes;
			bytes.resize(size_t(MAX_GRASS_SPECIES) * sizeof(uint32), 0);

			const uint32 count = (uint32)std::min<size_t>(meshIds.size(), MAX_GRASS_SPECIES);
			if (count > 0)
			{
				std::memcpy(bytes.data(), meshIds.data(), size_t(count) * sizeof(uint32));
			}

			renderer.UpdateBuffer(bufferId, std::move(bytes));
		};

		std::vector<uint32> lod0MeshIds(numSpecies);
		std::vector<uint32> lod1MeshIds(numSpecies);
		std::vector<uint32> lod2MeshIds(numSpecies);

		for (uint32 s = 0; s < numSpecies; ++s)
		{
			lod0MeshIds[s] = m_SpeciesIndirect[s].LOD0.MeshId;
			lod1MeshIds[s] = m_SpeciesIndirect[s].LOD1.MeshId;
			lod2MeshIds[s] = m_SpeciesIndirect[s].LOD2.MeshId;
		}

		uploadSpeciesMeshIdTable(STRING_HASH("Grass_SpeciesLOD0MeshId"), lod0MeshIds);
		uploadSpeciesMeshIdTable(STRING_HASH("Grass_SpeciesLOD1MeshId"), lod1MeshIds);
		uploadSpeciesMeshIdTable(STRING_HASH("Grass_SpeciesLOD2MeshId"), lod2MeshIds);

		// ---------------------------------------------------------------------
		// One-time init of tables using UpdateBuffer (CPU)
		// ---------------------------------------------------------------------
		{
			const int32 kIntMin = (int32)0x80000000u;

			std::vector<uint8> cells;
			cells.resize(size_t(visibleCells) * 16u, 0);

			for (uint32 i = 0u; i < visibleCells; ++i)
			{
				const uint32 poolIndex = i;
				std::memcpy(cells.data() + size_t(i) * 16u + 0u, &poolIndex, sizeof(uint32));

				int32 cc[2] = { kIntMin, kIntMin };
				std::memcpy(cells.data() + size_t(i) * 16u + 4u, cc, 8u);
			}
			renderer.UpdateBuffer(STRING_HASH("Grass_VisibleCellTable"), std::move(cells));

			std::vector<uint8> poolCoord;
			poolCoord.resize(size_t(numPools) * 16u, 0);

			for (uint32 i = 0u; i < numPools; ++i)
			{
				int32 cc[2] = { kIntMin, kIntMin };
				std::memcpy(poolCoord.data() + size_t(i) * 16u + 4u, cc, 8u);

				float h = 0.0f;
				std::memcpy(poolCoord.data() + size_t(i) * 16u + 12u, &h, 4u);
			}
			renderer.UpdateBuffer(STRING_HASH("Grass_PoolChunkCoord"), std::move(poolCoord));

			std::vector<uint8> dirty;
			dirty.resize(size_t(numPools) * 16u, 0);
			renderer.UpdateBuffer(STRING_HASH("Grass_PoolDirty"), std::move(dirty));
		}

		// =====================================================================
		// Pass A) UpdateChunkPools
		// =====================================================================
		renderer.AddPass(
			"Grass_UpdateChunkPools",
			EPassExecutionDomain::OutsideRenderPass,
			[&](RenderPassBuilder& b)
			{
				b.DeclareBufferUAV(STRING_HASH("Grass_VisibleCellTable"), RENDER_ACCESS_WRITE);
				b.DeclareBufferUAV(STRING_HASH("Grass_PoolChunkCoord"), RENDER_ACCESS_WRITE);
				b.DeclareBufferUAV(STRING_HASH("Grass_PoolDirty"), RENDER_ACCESS_WRITE);

				b.DeclareBufferCBVRead(STRING_HASH("GrassGenConstants"));
			},
			[this, &renderer, numSpecies](RenderPassContext& ctx)
			{
				ASSERT(ctx.pImmediateContext && ctx.pRegistry, "invalid ctx");
				ASSERT(m_pUpdatePoolsCSO && m_pUpdatePoolsSRB, "UpdatePools PSO/SRB not ready");

				IDeviceContext* pContext = ctx.pImmediateContext;

				// Upload constants once per frame (shared by all compute entries)
				{
					MapHelper<hlsl::GrassGenConstants> map(
						pContext,
						ctx.pRegistry->GetBuffer(STRING_HASH("GrassGenConstants")),
						MAP_WRITE,
						MAP_FLAG_DISCARD);

					// Visible window
					map->ChunkVisibleDim = 2u * m_ChunkHalfExtent;
					map->ChunkHalfExtent = (int)m_ChunkHalfExtent;
					map->NumPools = map->ChunkVisibleDim * map->ChunkVisibleDim;
					map->SamplesPerChunk = m_SamplesPerChunk;
					map->ChunkSize = m_ChunkSize;

					map->NumSpecies = numSpecies;

					// Global tuning (shared for now)
					// NOTE: If you want per-species tuning, extend constants to arrays.
					const GrassDesc& base = m_GrassDescs[0];
					map->LOD0Distance = base.LOD0Distance;
					map->LOD1Distance = base.LOD1Distance;
					map->LodHysteresis = base.LodHysteresis;

					map->YOffset = m_YOffset;
					map->Jitter = m_Jitter;
					map->NormalAlignStrength = m_NormalAlignStrength;

					map->MinScale = m_MinScale;
					map->MaxScale = m_MaxScale;
					map->SpawnProb = m_SpawnProb;
					map->SpawnRadius = m_SpawnRadius;

					map->BendStrengthMin = m_BendStrengthMin;
					map->BendStrengthMax = m_BendStrengthMax;
					map->SeedSalt = m_SeedSalt;

					map->DensityContrast = m_DensityContrast;
					map->DensityPow = m_DensityPow;
					map->SlopeToDensity = m_SlopeToDensity;

					map->HeightMinN = m_HeightMinN;
					map->HeightMaxN = m_HeightMaxN;
					map->HeightFadeN = m_HeightFadeN;

					map->InteractionInvWorldSizeXZ = float2{ 1.0f, 1.0f } / m_pInteractionSystem->GetWorldSizeXZ();
					map->InteractionOriginXZ = m_pInteractionSystem->GetWorldOriginXZ();

					const uint interactionResolution = m_pInteractionSystem->GetInteractionFieldResolution();
					map->InteractionInvFieldSize = float2{ 1.0f / interactionResolution, 1.0f / interactionResolution };
					map->InteractionTexelOrigin = m_pInteractionSystem->GetTexelOrigin();
				}

				pContext->SetPipelineState(m_pUpdatePoolsCSO);
				pContext->CommitShaderResources(m_pUpdatePoolsSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

				DispatchComputeAttribs disp = {};
				disp.ThreadGroupCountX = DivUp(2u * m_ChunkHalfExtent, 8u);
				disp.ThreadGroupCountY = DivUp(2u * m_ChunkHalfExtent, 8u);
				disp.ThreadGroupCountZ = 1;
				pContext->DispatchCompute(disp);
			},
				[this, &renderer]()
			{
				ShaderCreateInfo csCI = {};
				csCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
				csCI.EntryPoint = "UpdateChunkPoolsCS";
				csCI.Desc.Name = "Grass_UpdateChunkPoolsCS";
				csCI.Desc.ShaderType = SHADER_TYPE_COMPUTE;
				csCI.Desc.UseCombinedTextureSamplers = false;
				csCI.FilePath = m_GrassGenCS.c_str();

				RefCntAutoPtr<IShader> cs;
				renderer.CreateShader(csCI, &cs);
				ASSERT(cs, "UpdateChunkPoolsCS compile failed");

				ComputePipelineStateCreateInfo psoCI = {};
				psoCI.PSODesc.Name = "PSO_Grass_UpdateChunkPools";
				psoCI.PSODesc.PipelineType = PIPELINE_TYPE_COMPUTE;

				auto& rl = psoCI.PSODesc.ResourceLayout;
				rl.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

				ShaderResourceVariableDesc vars[] =
				{
					{ SHADER_TYPE_COMPUTE, "GRASS_GEN_CONSTANTS", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_VisibleCellTable",  SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_PoolChunkCoord",    SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_PoolDirty",         SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
				};
				rl.Variables = vars;
				rl.NumVariables = _countof(vars);

				psoCI.pCS = cs;

				m_pUpdatePoolsCSO = renderer.AcquirePipelineState(psoCI);
				ASSERT(m_pUpdatePoolsCSO, "AcquireCompute(UpdateChunkPools) failed");

				m_pUpdatePoolsCSO->CreateShaderResourceBinding(&m_pUpdatePoolsSRB, true);
				ASSERT(m_pUpdatePoolsSRB, "UpdatePools SRB create failed");

				if (auto* v = m_pUpdatePoolsSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "GRASS_GEN_CONSTANTS"))
				{
					v->Set(renderer.GetBuffer(STRING_HASH("GrassGenConstants")));
				}
				if (auto* v = m_pUpdatePoolsSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_VisibleCellTable"))
				{
					v->Set(renderer.GetBufferUAV(STRING_HASH("Grass_VisibleCellTable")));
				}
				if (auto* v = m_pUpdatePoolsSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_PoolChunkCoord"))
				{
					v->Set(renderer.GetBufferUAV(STRING_HASH("Grass_PoolChunkCoord")));
				}
				if (auto* v = m_pUpdatePoolsSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_PoolDirty"))
				{
					v->Set(renderer.GetBufferUAV(STRING_HASH("Grass_PoolDirty")));
				}
			});

		// =====================================================================
		// Pass B) FillNewPools
		// =====================================================================
		renderer.AddPass(
			"Grass_FillNewPools",
			EPassExecutionDomain::OutsideRenderPass,
			[&](RenderPassBuilder& b)
			{
				b.DeclareBufferUAV(STRING_HASH("Grass_PoolDirty"), RENDER_ACCESS_WRITE);
				b.DeclareBufferUAV(STRING_HASH("Grass_PoolPositions"), RENDER_ACCESS_WRITE);
				b.DeclareBufferUAV(STRING_HASH("Grass_PoolChunkCoord"), RENDER_ACCESS_WRITE);
				b.DeclareBufferUAV(STRING_HASH("Grass_VisibleCellTable"), RENDER_ACCESS_READ);

				b.DeclareTextureSRVRead(STRING_HASH("TerrainVegetation"));
				b.DeclareTextureSRVRead(STRING_HASH("InteractionField"));
				b.DeclareTextureSRVRead(STRING_HASH("TerrainHeight"));

				b.DeclareBufferCBVRead(STRING_HASH("GrassGenConstants"));
			},
			[this, &renderer](RenderPassContext& ctx)
			{
				ASSERT(ctx.pImmediateContext && ctx.pRegistry, "invalid ctx");
				ASSERT(m_pFillNewPoolsCSO && m_pFillNewPoolsSRB, "FillNewPools PSO/SRB not ready");

				IDeviceContext* pContext = ctx.pImmediateContext;

				{
					StateTransitionDesc tr =
					{
						renderer.GetTexture(STRING_HASH("TerrainHeight")),
						RESOURCE_STATE_UNKNOWN,
						RESOURCE_STATE_SHADER_RESOURCE,
						STATE_TRANSITION_FLAG_UPDATE_STATE
					};
					pContext->TransitionResourceStates(1, &tr);
				}

				pContext->SetPipelineState(m_pFillNewPoolsCSO);
				pContext->CommitShaderResources(m_pFillNewPoolsSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

				DispatchComputeAttribs disp = {};
				disp.ThreadGroupCountX = (2u * m_ChunkHalfExtent) * (2u * m_ChunkHalfExtent);
				disp.ThreadGroupCountY = 1;
				disp.ThreadGroupCountZ = 1;
				pContext->DispatchCompute(disp);
			},
				[this, &renderer]()
			{
				ShaderCreateInfo csCI = {};
				csCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
				csCI.EntryPoint = "FillNewPoolsCS";
				csCI.Desc.Name = "Grass_FillNewPoolsCS";
				csCI.Desc.ShaderType = SHADER_TYPE_COMPUTE;
				csCI.Desc.UseCombinedTextureSamplers = false;
				csCI.FilePath = m_GrassGenCS.c_str();

				RefCntAutoPtr<IShader> cs;
				renderer.CreateShader(csCI, &cs);
				ASSERT(cs, "FillNewPoolsCS compile failed");

				ComputePipelineStateCreateInfo psoCI = {};
				psoCI.PSODesc.Name = "PSO_Grass_FillNewPools";
				psoCI.PSODesc.PipelineType = PIPELINE_TYPE_COMPUTE;

				auto& rl = psoCI.PSODesc.ResourceLayout;
				rl.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

				ShaderResourceVariableDesc vars[] =
				{
					{ SHADER_TYPE_COMPUTE, "GRASS_GEN_CONSTANTS", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_VisibleCellTable",  SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_PoolDirty",         SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_PoolChunkCoord",    SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_PoolPositions",     SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
				};
				rl.Variables = vars;
				rl.NumVariables = _countof(vars);

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
					{ SHADER_TYPE_COMPUTE, "g_LinearClampSampler", linearClamp },
					{ SHADER_TYPE_COMPUTE, "g_LinearWrapSampler",  linearWrap  },
				};
				rl.ImmutableSamplers = samplers;
				rl.NumImmutableSamplers = _countof(samplers);

				psoCI.pCS = cs;

				m_pFillNewPoolsCSO = renderer.AcquirePipelineState(psoCI);
				ASSERT(m_pFillNewPoolsCSO, "AcquireCompute(FillNewPools) failed");

				m_pFillNewPoolsCSO->CreateShaderResourceBinding(&m_pFillNewPoolsSRB, true);
				ASSERT(m_pFillNewPoolsSRB, "FillNewPools SRB create failed");

				if (auto* v = m_pFillNewPoolsSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "GRASS_GEN_CONSTANTS"))
				{
					v->Set(renderer.GetBuffer(STRING_HASH("GrassGenConstants")));
				}
				if (auto* v = m_pFillNewPoolsSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_VisibleCellTable"))
				{
					v->Set(renderer.GetBufferUAV(STRING_HASH("Grass_VisibleCellTable")));
				}
				if (auto* v = m_pFillNewPoolsSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_PoolDirty"))
				{
					v->Set(renderer.GetBufferUAV(STRING_HASH("Grass_PoolDirty")));
				}
				if (auto* v = m_pFillNewPoolsSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_PoolChunkCoord"))
				{
					v->Set(renderer.GetBufferUAV(STRING_HASH("Grass_PoolChunkCoord")));
				}
				if (auto* v = m_pFillNewPoolsSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_PoolPositions"))
				{
					v->Set(renderer.GetBufferUAV(STRING_HASH("Grass_PoolPositions")));
				}
			});

		// =====================================================================
		// Pass B-1) ClearSpeciesCounters
		// =====================================================================
		renderer.AddPass(
			"Grass_ClearSpeciesCounters",
			EPassExecutionDomain::OutsideRenderPass,
			[&](RenderPassBuilder& b)
			{
				b.DeclareBufferUAV(STRING_HASH("Grass_SpeciesLodCounts"), RENDER_ACCESS_WRITE);
				b.DeclareBufferUAV(STRING_HASH("Grass_SpeciesLodOffsets"), RENDER_ACCESS_WRITE);
				b.DeclareBufferUAV(STRING_HASH("Grass_SpeciesLodWriteCounters"), RENDER_ACCESS_WRITE);
			},
			[this](RenderPassContext& ctx)
			{
				ASSERT(ctx.pImmediateContext && ctx.pRegistry, "invalid ctx");
				ASSERT(m_pClearSpeciesCSO && m_pClearSpeciesSRB, "ClearSpecies PSO/SRB not ready");

				IDeviceContext* pContext = ctx.pImmediateContext;

				pContext->SetPipelineState(m_pClearSpeciesCSO);
				pContext->CommitShaderResources(m_pClearSpeciesSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

				DispatchComputeAttribs disp = {};
				disp.ThreadGroupCountX = 1;
				disp.ThreadGroupCountY = 1;
				disp.ThreadGroupCountZ = 1;
				pContext->DispatchCompute(disp);
			},
				[this, &renderer]()
			{
				ShaderCreateInfo csCI = {};
				csCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
				csCI.EntryPoint = "ClearSpeciesCountersCS";
				csCI.Desc.Name = "Grass_ClearSpeciesCountersCS";
				csCI.Desc.ShaderType = SHADER_TYPE_COMPUTE;
				csCI.Desc.UseCombinedTextureSamplers = false;
				csCI.FilePath = m_GrassGenCS.c_str();

				RefCntAutoPtr<IShader> cs;
				renderer.CreateShader(csCI, &cs);
				ASSERT(cs, "ClearSpeciesCountersCS compile failed");

				ComputePipelineStateCreateInfo psoCI = {};
				psoCI.PSODesc.Name = "PSO_Grass_ClearSpeciesCounters";
				psoCI.PSODesc.PipelineType = PIPELINE_TYPE_COMPUTE;

				auto& rl = psoCI.PSODesc.ResourceLayout;
				rl.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

				ShaderResourceVariableDesc vars[] =
				{
					{ SHADER_TYPE_COMPUTE, "g_SpeciesLodCounts",        SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_SpeciesLodOffsets",       SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_SpeciesLodWriteCounters", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
				};
				rl.Variables = vars;
				rl.NumVariables = _countof(vars);

				psoCI.pCS = cs;

				m_pClearSpeciesCSO = renderer.AcquirePipelineState(psoCI);
				ASSERT(m_pClearSpeciesCSO, "AcquireCompute(ClearSpeciesCounters) failed");

				m_pClearSpeciesCSO->CreateShaderResourceBinding(&m_pClearSpeciesSRB, true);
				ASSERT(m_pClearSpeciesSRB, "ClearSpecies SRB create failed");

				if (auto* v = m_pClearSpeciesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_SpeciesLodCounts"))
				{
					v->Set(renderer.GetBufferUAV(STRING_HASH("Grass_SpeciesLodCounts")));
				}
				if (auto* v = m_pClearSpeciesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_SpeciesLodOffsets"))
				{
					v->Set(renderer.GetBufferUAV(STRING_HASH("Grass_SpeciesLodOffsets")));
				}
				if (auto* v = m_pClearSpeciesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_SpeciesLodWriteCounters"))
				{
					v->Set(renderer.GetBufferUAV(STRING_HASH("Grass_SpeciesLodWriteCounters")));
				}
			});

		// =====================================================================
		// Pass B-2) CountInstancesFromPools (species/lod counts)
		// =====================================================================
		renderer.AddPass(
			"Grass_CountSpeciesInstances",
			EPassExecutionDomain::OutsideRenderPass,
			[&](RenderPassBuilder& b)
			{
				b.DeclareBufferUAV(STRING_HASH("Grass_PoolPositions"), RENDER_ACCESS_READ);
				b.DeclareBufferUAV(STRING_HASH("Grass_VisibleCellTable"), RENDER_ACCESS_READ);
				b.DeclareBufferUAV(STRING_HASH("Grass_PoolChunkCoord"), RENDER_ACCESS_READ);

				b.DeclareBufferUAV(STRING_HASH("Grass_SpeciesLodCounts"), RENDER_ACCESS_WRITE);

				b.DeclareTextureSRVRead(STRING_HASH("TerrainVegetation"));
				b.DeclareTextureSRVRead(STRING_HASH("InteractionField"));
				b.DeclareTextureSRVRead(STRING_HASH("TerrainHeight"));

				b.DeclareBufferCBVRead(STRING_HASH("GrassGenConstants"));
			},
			[this, &renderer](RenderPassContext& ctx)
			{
				ASSERT(ctx.pImmediateContext && ctx.pRegistry, "invalid ctx");
				ASSERT(m_pCountSpeciesCSO && m_pCountSpeciesSRB, "CountSpecies PSO/SRB not ready");

				IDeviceContext* pContext = ctx.pImmediateContext;

				pContext->SetPipelineState(m_pCountSpeciesCSO);
				pContext->CommitShaderResources(m_pCountSpeciesSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

				DispatchComputeAttribs disp = {};
				disp.ThreadGroupCountX = (2u * m_ChunkHalfExtent) * (2u * m_ChunkHalfExtent);
				disp.ThreadGroupCountY = 1;
				disp.ThreadGroupCountZ = 1;
				pContext->DispatchCompute(disp);
			},
				[this, &renderer]()
			{
				ShaderCreateInfo csCI = {};
				csCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
				csCI.EntryPoint = "CountInstancesFromPoolsCS";
				csCI.Desc.Name = "Grass_CountInstancesFromPoolsCS";
				csCI.Desc.ShaderType = SHADER_TYPE_COMPUTE;
				csCI.Desc.UseCombinedTextureSamplers = false;
				csCI.FilePath = m_GrassGenCS.c_str();

				RefCntAutoPtr<IShader> cs;
				renderer.CreateShader(csCI, &cs);
				ASSERT(cs, "CountInstancesFromPoolsCS compile failed");

				ComputePipelineStateCreateInfo psoCI = {};
				psoCI.PSODesc.Name = "PSO_Grass_CountSpeciesInstances";
				psoCI.PSODesc.PipelineType = PIPELINE_TYPE_COMPUTE;

				auto& rl = psoCI.PSODesc.ResourceLayout;
				rl.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

				ShaderResourceVariableDesc vars[] =
				{
					{ SHADER_TYPE_COMPUTE, "GRASS_GEN_CONSTANTS",   SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_VisibleCellTable",    SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_PoolPositions",       SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_PoolChunkCoord",      SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_SpeciesLodCounts",    SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_InteractionField",    SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
				};
				rl.Variables = vars;
				rl.NumVariables = _countof(vars);

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
					{ SHADER_TYPE_COMPUTE, "g_LinearClampSampler", linearClamp },
					{ SHADER_TYPE_COMPUTE, "g_LinearWrapSampler",  linearWrap  },
				};
				rl.ImmutableSamplers = samplers;
				rl.NumImmutableSamplers = _countof(samplers);

				psoCI.pCS = cs;

				m_pCountSpeciesCSO = renderer.AcquirePipelineState(psoCI);
				ASSERT(m_pCountSpeciesCSO, "AcquireCompute(CountSpeciesInstances) failed");

				m_pCountSpeciesCSO->CreateShaderResourceBinding(&m_pCountSpeciesSRB, true);
				ASSERT(m_pCountSpeciesSRB, "CountSpecies SRB create failed");

				if (auto* v = m_pCountSpeciesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "GRASS_GEN_CONSTANTS"))
				{
					v->Set(renderer.GetBuffer(STRING_HASH("GrassGenConstants")));
				}
				if (auto* v = m_pCountSpeciesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_VisibleCellTable"))
				{
					v->Set(renderer.GetBufferUAV(STRING_HASH("Grass_VisibleCellTable")));
				}
				if (auto* v = m_pCountSpeciesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_PoolPositions"))
				{
					v->Set(renderer.GetBufferUAV(STRING_HASH("Grass_PoolPositions")));
				}
				if (auto* v = m_pCountSpeciesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_PoolChunkCoord"))
				{
					v->Set(renderer.GetBufferUAV(STRING_HASH("Grass_PoolChunkCoord")));
				}
				if (auto* v = m_pCountSpeciesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_SpeciesLodCounts"))
				{
					v->Set(renderer.GetBufferUAV(STRING_HASH("Grass_SpeciesLodCounts")));
				}
				if (auto* v = m_pCountSpeciesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_InteractionField"))
				{
					v->Set(renderer.GetTextureSRV(STRING_HASH("InteractionField")), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
				}
			});

		// =====================================================================
		// Pass B-3) PrefixSpeciesOffsets (exclusive scan)
		// =====================================================================
		renderer.AddPass(
			"Grass_PrefixSpeciesOffsets",
			EPassExecutionDomain::OutsideRenderPass,
			[&](RenderPassBuilder& b)
			{
				b.DeclareBufferUAV(STRING_HASH("Grass_SpeciesLodCounts"), RENDER_ACCESS_READ);
				b.DeclareBufferUAV(STRING_HASH("Grass_SpeciesLodOffsets"), RENDER_ACCESS_WRITE);
				b.DeclareBufferUAV(STRING_HASH("Grass_SpeciesLodWriteCounters"), RENDER_ACCESS_WRITE);

				b.DeclareBufferCBVRead(STRING_HASH("GrassGenConstants"));
			},
			[this](RenderPassContext& ctx)
			{
				ASSERT(ctx.pImmediateContext && ctx.pRegistry, "invalid ctx");
				ASSERT(m_pPrefixSpeciesCSO && m_pPrefixSpeciesSRB, "PrefixSpecies PSO/SRB not ready");

				IDeviceContext* pContext = ctx.pImmediateContext;

				pContext->SetPipelineState(m_pPrefixSpeciesCSO);
				pContext->CommitShaderResources(m_pPrefixSpeciesSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

				DispatchComputeAttribs disp = {};
				disp.ThreadGroupCountX = 1;
				disp.ThreadGroupCountY = 1;
				disp.ThreadGroupCountZ = 1;
				pContext->DispatchCompute(disp);
			},
				[this, &renderer]()
			{
				ShaderCreateInfo csCI = {};
				csCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
				csCI.EntryPoint = "PrefixSpeciesOffsetsCS";
				csCI.Desc.Name = "Grass_PrefixSpeciesOffsetsCS";
				csCI.Desc.ShaderType = SHADER_TYPE_COMPUTE;
				csCI.Desc.UseCombinedTextureSamplers = false;
				csCI.FilePath = m_GrassGenCS.c_str();

				RefCntAutoPtr<IShader> cs;
				renderer.CreateShader(csCI, &cs);
				ASSERT(cs, "PrefixSpeciesOffsetsCS compile failed");

				ComputePipelineStateCreateInfo psoCI = {};
				psoCI.PSODesc.Name = "PSO_Grass_PrefixSpeciesOffsets";
				psoCI.PSODesc.PipelineType = PIPELINE_TYPE_COMPUTE;

				auto& rl = psoCI.PSODesc.ResourceLayout;
				rl.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

				ShaderResourceVariableDesc vars[] =
				{
					{ SHADER_TYPE_COMPUTE, "GRASS_GEN_CONSTANTS",        SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_SpeciesLodCounts",         SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_SpeciesLodOffsets",        SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_SpeciesLodWriteCounters",  SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
				};
				rl.Variables = vars;
				rl.NumVariables = _countof(vars);

				psoCI.pCS = cs;

				m_pPrefixSpeciesCSO = renderer.AcquirePipelineState(psoCI);
				ASSERT(m_pPrefixSpeciesCSO, "AcquireCompute(PrefixSpeciesOffsets) failed");

				m_pPrefixSpeciesCSO->CreateShaderResourceBinding(&m_pPrefixSpeciesSRB, true);
				ASSERT(m_pPrefixSpeciesSRB, "PrefixSpecies SRB create failed");

				if (auto* v = m_pPrefixSpeciesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "GRASS_GEN_CONSTANTS"))
				{
					v->Set(renderer.GetBuffer(STRING_HASH("GrassGenConstants")));
				}
				if (auto* v = m_pPrefixSpeciesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_SpeciesLodCounts"))
				{
					v->Set(renderer.GetBufferUAV(STRING_HASH("Grass_SpeciesLodCounts")));
				}
				if (auto* v = m_pPrefixSpeciesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_SpeciesLodOffsets"))
				{
					v->Set(renderer.GetBufferUAV(STRING_HASH("Grass_SpeciesLodOffsets")));
				}
				if (auto* v = m_pPrefixSpeciesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_SpeciesLodWriteCounters"))
				{
					v->Set(renderer.GetBufferUAV(STRING_HASH("Grass_SpeciesLodWriteCounters")));
				}
			});

		// =====================================================================
		// Pass C) BuildInstancesFromPools (packed by species, still mesh-counted)
		// =====================================================================
		renderer.AddPass(
			"Grass_BuildInstancesFromPools",
			EPassExecutionDomain::OutsideRenderPass,
			[&](RenderPassBuilder& b)
			{
				b.DeclareBufferUAV(STRING_HASH("Grass_PoolPositions"), RENDER_ACCESS_READ);
				b.DeclareBufferUAV(STRING_HASH("Grass_VisibleCellTable"), RENDER_ACCESS_READ);
				b.DeclareBufferUAV(STRING_HASH("Grass_PoolChunkCoord"), RENDER_ACCESS_READ);

				b.DeclareBufferUAV(STRING_HASH("GrassInstanceBufferLOD0"), RENDER_ACCESS_WRITE);
				b.DeclareBufferUAV(STRING_HASH("GrassInstanceBufferLOD1"), RENDER_ACCESS_WRITE);
				b.DeclareBufferUAV(STRING_HASH("GrassInstanceBufferLOD2"), RENDER_ACCESS_WRITE);

				b.DeclareBufferUAV(STRING_HASH("IndirectMeshInstanceCountBuffer"), RENDER_ACCESS_WRITE);

				b.DeclareBufferUAV(STRING_HASH("Grass_SpeciesLodOffsets"), RENDER_ACCESS_READ);
				b.DeclareBufferUAV(STRING_HASH("Grass_SpeciesLodWriteCounters"), RENDER_ACCESS_WRITE);

				b.DeclareTextureSRVRead(STRING_HASH("TerrainVegetation"));
				b.DeclareTextureSRVRead(STRING_HASH("InteractionField"));
				b.DeclareTextureSRVRead(STRING_HASH("TerrainHeight"));

				b.DeclareBufferCBVRead(STRING_HASH("GrassGenConstants"));
			},
			[this, &renderer](RenderPassContext& ctx)
			{
				ASSERT(ctx.pImmediateContext && ctx.pRegistry, "invalid ctx");
				ASSERT(m_pBuildInstancesCSO && m_pBuildInstancesSRB, "BuildInstances PSO/SRB not ready");

				IDeviceContext* pContext = ctx.pImmediateContext;

				pContext->SetPipelineState(m_pBuildInstancesCSO);
				pContext->CommitShaderResources(m_pBuildInstancesSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

				DispatchComputeAttribs disp = {};
				disp.ThreadGroupCountX = (2u * m_ChunkHalfExtent) * (2u * m_ChunkHalfExtent);
				disp.ThreadGroupCountY = 1;
				disp.ThreadGroupCountZ = 1;
				pContext->DispatchCompute(disp);
			},
				[this, &renderer]()
			{
				ShaderCreateInfo csCI = {};
				csCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
				csCI.EntryPoint = "BuildInstancesFromPoolsCS";
				csCI.Desc.Name = "Grass_BuildInstancesFromPoolsCS";
				csCI.Desc.ShaderType = SHADER_TYPE_COMPUTE;
				csCI.Desc.UseCombinedTextureSamplers = false;
				csCI.FilePath = m_GrassGenCS.c_str();

				RefCntAutoPtr<IShader> cs;
				renderer.CreateShader(csCI, &cs);
				ASSERT(cs, "BuildInstancesFromPoolsCS compile failed");

				ComputePipelineStateCreateInfo psoCI = {};
				psoCI.PSODesc.Name = "PSO_Grass_BuildInstancesFromPools";
				psoCI.PSODesc.PipelineType = PIPELINE_TYPE_COMPUTE;

				auto& rl = psoCI.PSODesc.ResourceLayout;
				rl.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

				ShaderResourceVariableDesc vars[] =
				{
					{ SHADER_TYPE_COMPUTE, "GRASS_GEN_CONSTANTS",         SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_VisibleCellTable",          SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_PoolPositions",             SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_PoolChunkCoord",            SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_MeshInstanceCountBuffer",   SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_InteractionField",          SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_SpeciesLodOffsets",         SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_SpeciesLodWriteCounters",   SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_SpeciesLOD0MeshId", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_SpeciesLOD1MeshId", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_SpeciesLOD2MeshId", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
				};
				rl.Variables = vars;
				rl.NumVariables = _countof(vars);

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
					{ SHADER_TYPE_COMPUTE, "g_LinearClampSampler", linearClamp },
					{ SHADER_TYPE_COMPUTE, "g_LinearWrapSampler",  linearWrap  },
				};
				rl.ImmutableSamplers = samplers;
				rl.NumImmutableSamplers = _countof(samplers);

				psoCI.pCS = cs;

				m_pBuildInstancesCSO = renderer.AcquirePipelineState(psoCI);
				ASSERT(m_pBuildInstancesCSO, "AcquireCompute(BuildInstancesFromPools) failed");

				if (auto* pVar = m_pBuildInstancesCSO->GetStaticVariableByName(SHADER_TYPE_COMPUTE, "g_OutInstancesLOD0"))
				{
					pVar->Set(renderer.GetBufferUAV(STRING_HASH("GrassInstanceBufferLOD0")));
				}
				if (auto* pVar = m_pBuildInstancesCSO->GetStaticVariableByName(SHADER_TYPE_COMPUTE, "g_OutInstancesLOD1"))
				{
					pVar->Set(renderer.GetBufferUAV(STRING_HASH("GrassInstanceBufferLOD1")));
				}
				if (auto* pVar = m_pBuildInstancesCSO->GetStaticVariableByName(SHADER_TYPE_COMPUTE, "g_OutInstancesLOD2"))
				{
					pVar->Set(renderer.GetBufferUAV(STRING_HASH("GrassInstanceBufferLOD2")));
				}

				m_pBuildInstancesCSO->CreateShaderResourceBinding(&m_pBuildInstancesSRB, true);
				ASSERT(m_pBuildInstancesSRB, "BuildInstances SRB create failed");

				if (auto* v = m_pBuildInstancesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "GRASS_GEN_CONSTANTS"))
				{
					v->Set(renderer.GetBuffer(STRING_HASH("GrassGenConstants")));
				}
				if (auto* v = m_pBuildInstancesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_VisibleCellTable"))
				{
					v->Set(renderer.GetBufferUAV(STRING_HASH("Grass_VisibleCellTable")));
				}
				if (auto* v = m_pBuildInstancesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_PoolPositions"))
				{
					v->Set(renderer.GetBufferUAV(STRING_HASH("Grass_PoolPositions")));
				}
				if (auto* v = m_pBuildInstancesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_PoolChunkCoord"))
				{
					v->Set(renderer.GetBufferUAV(STRING_HASH("Grass_PoolChunkCoord")));
				}
				if (auto* v = m_pBuildInstancesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_MeshInstanceCountBuffer"))
				{
					v->Set(renderer.GetBufferUAV(STRING_HASH("IndirectMeshInstanceCountBuffer")));
				}
				if (auto* v = m_pBuildInstancesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_InteractionField"))
				{
					v->Set(renderer.GetTextureSRV(STRING_HASH("InteractionField")), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
				}
				if (auto* v = m_pBuildInstancesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_SpeciesLodOffsets"))
				{
					v->Set(renderer.GetBufferUAV(STRING_HASH("Grass_SpeciesLodOffsets")));
				}
				if (auto* v = m_pBuildInstancesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_SpeciesLodWriteCounters"))
				{
					v->Set(renderer.GetBufferUAV(STRING_HASH("Grass_SpeciesLodWriteCounters")));
				}
				if (auto* v = m_pBuildInstancesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_SpeciesLOD0MeshId"))
				{
					v->Set(renderer.GetBufferSRV(STRING_HASH("Grass_SpeciesLOD0MeshId")), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
				}
				if (auto* v = m_pBuildInstancesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_SpeciesLOD1MeshId"))
				{
					v->Set(renderer.GetBufferSRV(STRING_HASH("Grass_SpeciesLOD1MeshId")), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
				}
				if (auto* v = m_pBuildInstancesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_SpeciesLOD2MeshId"))
				{
					v->Set(renderer.GetBufferSRV(STRING_HASH("Grass_SpeciesLOD2MeshId")), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
				}
			});
	}

} // namespace shz
