#include "pch.h"
#include "Engine/RenderSystem/Public/GrassSystem.h"

#include <cstring>
#include <algorithm>

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

	static void BuildSpeciesTypeTables(
		const std::vector<GrassDesc>& species,
		std::vector<uint32>& outSpeciesVarOffset,
		std::vector<uint32>& outSpeciesVarCount,
		std::vector<uint32>& outTypeToSpecies,
		std::vector<uint32>& outTypeToVariation)
	{
		const uint32 numSpecies = (uint32)species.size();

		outSpeciesVarOffset.assign(numSpecies + 1u, 0u);
		outSpeciesVarCount.assign(numSpecies, 0u);
		outTypeToSpecies.clear();
		outTypeToVariation.clear();

		uint32 running = 0u;
		for (uint32 s = 0u; s < numSpecies; ++s)
		{
			const uint32 vCount = std::max<uint32>(1u, species[s].GetNumVariations());
			outSpeciesVarOffset[s] = running;
			outSpeciesVarCount[s] = vCount;

			for (uint32 v = 0u; v < vCount; ++v)
			{
				outTypeToSpecies.push_back(s);
				outTypeToVariation.push_back(v);
				++running;
			}
		}
		outSpeciesVarOffset[numSpecies] = running;
	}

	void GrassSystem::RebuildGroupTables()
	{
		m_BaseSpeciesIds.clear();
		m_SpecialSpeciesIds.clear();

		const uint32 n = (uint32)m_GrassDescs.size();
		for (uint32 s = 0u; s < n; ++s)
		{
			if (m_GrassDescs[s].IsSpecial) m_SpecialSpeciesIds.push_back(s);
			else m_BaseSpeciesIds.push_back(s);
		}

		// Ensure at least 1 base species (fallback).
		if (m_BaseSpeciesIds.empty() && n > 0u) m_BaseSpeciesIds.push_back(0u);
	}

	void GrassSystem::Initialize(Renderer& renderer)
	{
		BufferDesc bd = {};
		bd.Name = "GrassRenderConstantsCB";
		bd.Usage = USAGE_DYNAMIC;
		bd.BindFlags = BIND_UNIFORM_BUFFER;
		bd.CPUAccessFlags = CPU_ACCESS_WRITE;
		bd.Size = sizeof(hlsl::GrassRenderConstants);

		renderer.AddBuffer(STRING_HASH("GrassRenderConstantsCB"), bd);
		renderer.RegisterStaticBufferCBV("GRASS_RENDER_CONSTANTS", STRING_HASH("GrassRenderConstantsCB"));
	}

	void GrassSystem::InstallPasses(Renderer& renderer, RenderScene& scene, const InteractionSystem& interaction)
	{
		m_pInteractionSystem = &interaction;

		ASSERT(!m_GrassDescs.empty(), "No grass species. Call AddBaseGrass/AddSpecialGrass.");

		for (uint32 s = 0u; s < (uint32)m_GrassDescs.size(); ++s)
		{
			const GrassDesc& gd = m_GrassDescs[s];

			ASSERT(gd.Weight >= 0.0f, "GrassDesc.Weight must be >= 0");
			ASSERT(!gd.Variations.empty(), "GrassDesc.Variations is empty.");

			ASSERT(gd.MinScale > 0.0f, "GrassDesc.MinScale must be > 0");
			ASSERT(gd.MaxScale >= gd.MinScale, "GrassDesc.MaxScale must be >= MinScale");

			ASSERT(gd.BendStrengthMin >= 0.0f, "GrassDesc.BendStrengthMin must be >= 0");
			ASSERT(gd.BendStrengthMax >= gd.BendStrengthMin, "GrassDesc.BendStrengthMax must be >= BendStrengthMin");

			ASSERT(gd.ClusterStrength >= 0.0f, "ClusterStrength must be >= 0");
			ASSERT(gd.ClusterScale >= 0.0f, "ClusterScale must be >= 0");
			ASSERT(gd.ClusterJitter >= 0.0f, "ClusterJitter must be >= 0");

			for (auto* pMesh : gd.Variations) ASSERT(pMesh, "Variation mesh is null.");
		}

		BuildSpeciesTypeTables(m_GrassDescs, m_SpeciesVarOffset, m_SpeciesVarCount, m_TypeToSpecies, m_TypeToVariation);
		RebuildGroupTables();

		const uint32 numSpecies = (uint32)m_GrassDescs.size();
		const uint32 numTypes = (uint32)m_TypeToSpecies.size();

		ASSERT(numTypes <= MAX_GRASS_SPECIES, "Total (species*variations) exceeds MAX_GRASS_SPECIES.");

		const uint32 visibleDim = 2u * m_ChunkHalfExtent;
		const uint32 visibleCells = visibleDim * visibleDim;
		const uint32 numPools = visibleCells;

		// ---------------------------------------------------------------------
		// Instance buffers
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
		// Chunk pool buffers
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
		// Type/LOD packing buffers
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

		// TypeId -> MeshId lookup
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

		// Base/Special selection tables
		{
			BufferDesc bd = {};
			bd.Usage = USAGE_DEFAULT;
			bd.BindFlags = BIND_SHADER_RESOURCE;
			bd.Mode = BUFFER_MODE_STRUCTURED;

			bd.ElementByteStride = 4;
			bd.Size = uint64(MAX_GRASS_SPECIES) * 4ull;

			bd.Name = "Grass_BaseSpeciesWeightPrefix";
			renderer.AddBuffer(STRING_HASH("Grass_BaseSpeciesWeightPrefix"), bd);

			bd.Name = "Grass_SpecialSpeciesWeightPrefix";
			renderer.AddBuffer(STRING_HASH("Grass_SpecialSpeciesWeightPrefix"), bd);

			bd.Name = "Grass_BaseSpeciesIds";
			renderer.AddBuffer(STRING_HASH("Grass_BaseSpeciesIds"), bd);

			bd.Name = "Grass_SpecialSpeciesIds";
			renderer.AddBuffer(STRING_HASH("Grass_SpecialSpeciesIds"), bd);
		}

		// Species variation offset/count & map
		{
			BufferDesc bd = {};
			bd.Usage = USAGE_DEFAULT;
			bd.BindFlags = BIND_SHADER_RESOURCE;
			bd.Mode = BUFFER_MODE_STRUCTURED;
			bd.ElementByteStride = 4;

			bd.Size = uint64(MAX_GRASS_SPECIES + 1u) * 4ull;
			bd.Name = "Grass_SpeciesVarOffsets";
			renderer.AddBuffer(STRING_HASH("Grass_SpeciesVarOffsets"), bd);

			bd.Size = uint64(MAX_GRASS_SPECIES) * 4ull;
			bd.Name = "Grass_SpeciesVarCounts";
			renderer.AddBuffer(STRING_HASH("Grass_SpeciesVarCounts"), bd);

			bd.Size = uint64(MAX_GRASS_SPECIES) * 4ull;
			bd.Name = "Grass_SpeciesVarToTypeId";
			renderer.AddBuffer(STRING_HASH("Grass_SpeciesVarToTypeId"), bd);
		}

		// Per-type params (float4)
		{
			BufferDesc bd = {};
			bd.Usage = USAGE_DEFAULT;
			bd.BindFlags = BIND_SHADER_RESOURCE;
			bd.Mode = BUFFER_MODE_STRUCTURED;
			bd.ElementByteStride = 16;
			bd.Size = uint64(MAX_GRASS_SPECIES) * 16ull;

			bd.Name = "Grass_TypeParams0";
			renderer.AddBuffer(STRING_HASH("Grass_TypeParams0"), bd);
		}

		// Per-species cluster params (float4)
		{
			BufferDesc bd = {};
			bd.Usage = USAGE_DEFAULT;
			bd.BindFlags = BIND_SHADER_RESOURCE;
			bd.Mode = BUFFER_MODE_STRUCTURED;
			bd.ElementByteStride = 16;
			bd.Size = uint64(MAX_GRASS_SPECIES) * 16ull;

			bd.Name = "Grass_SpeciesClusterParams";
			renderer.AddBuffer(STRING_HASH("Grass_SpeciesClusterParams"), bd);
		}

		// Constants CB
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
		// Indirect allocation per type
		// ---------------------------------------------------------------------
		m_TypeIndirect.clear();
		m_TypeIndirect.resize(numTypes);

		std::vector<uint32> lod0MeshIds(numTypes);
		std::vector<uint32> lod1MeshIds(numTypes);
		std::vector<uint32> lod2MeshIds(numTypes);

		for (uint32 typeId = 0u; typeId < numTypes; ++typeId)
		{
			const uint32 sp = m_TypeToSpecies[typeId];
			const uint32 vr = m_TypeToVariation[typeId];

			const GrassDesc& gd = m_GrassDescs[sp];
			const StaticMeshRenderData* pMesh = gd.Variations[vr];
			ASSERT(pMesh, "Type mesh is null");

			const uint32 lod0Sections = (uint32)pMesh->GetLevel(0).Sections.size();
			const uint32 lod1Sections = (uint32)pMesh->GetLevel(1).Sections.size();
			const uint32 lod2Sections = (uint32)pMesh->GetLevel(2).Sections.size();

			m_TypeIndirect[typeId].LOD0 = renderer.GetIndirectArgsSystem()->AllocateMesh("GrassLOD0_Type", lod0Sections);
			m_TypeIndirect[typeId].LOD1 = renderer.GetIndirectArgsSystem()->AllocateMesh("GrassLOD1_Type", lod1Sections);
			m_TypeIndirect[typeId].LOD2 = renderer.GetIndirectArgsSystem()->AllocateMesh("GrassLOD2_Type", lod2Sections);

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

					renderer.GetIndirectArgsSystem()->SetTemplate(baseSlot + si, t);
				}
			};

			SetMeshTemplates(pMesh->GetLevel(0), m_TypeIndirect[typeId].LOD0.BaseSlot);
			SetMeshTemplates(pMesh->GetLevel(1), m_TypeIndirect[typeId].LOD1.BaseSlot);
			SetMeshTemplates(pMesh->GetLevel(2), m_TypeIndirect[typeId].LOD2.BaseSlot);

			lod0MeshIds[typeId] = m_TypeIndirect[typeId].LOD0.MeshId;
			lod1MeshIds[typeId] = m_TypeIndirect[typeId].LOD1.MeshId;
			lod2MeshIds[typeId] = m_TypeIndirect[typeId].LOD2.MeshId;

			// Scene indirect
			{
				RenderScene::IndirectObjectDesc d = {};
				d.bCastShadow = false;
				d.bDepthPrepass = false;
				d.PassKey = STRING_HASH("GBuffer");

				d.pMesh = &pMesh->GetLevel(0);
				d.IndirectBaseSlot = m_TypeIndirect[typeId].LOD0.BaseSlot;
				d.IndirectMeshId = m_TypeIndirect[typeId].LOD0.MeshId;
				d.StartInstanceLocation = typeId * 3u + 0u;
				scene.AddIndirect(d);

				d.pMesh = &pMesh->GetLevel(1);
				d.IndirectBaseSlot = m_TypeIndirect[typeId].LOD1.BaseSlot;
				d.IndirectMeshId = m_TypeIndirect[typeId].LOD1.MeshId;
				d.StartInstanceLocation = typeId * 3u + 1u;
				scene.AddIndirect(d);

				d.pMesh = &pMesh->GetLevel(2);
				d.IndirectBaseSlot = m_TypeIndirect[typeId].LOD2.BaseSlot;
				d.IndirectMeshId = m_TypeIndirect[typeId].LOD2.MeshId;
				d.StartInstanceLocation = typeId * 3u + 2u;
				scene.AddIndirect(d);
			}
		}

		// ---------------------------------------------------------------------
		// Upload helpers
		// ---------------------------------------------------------------------
		auto uploadU32Table = [&](uint64 bufferId, const std::vector<uint32>& values, uint32 maxCount)
		{
			std::vector<uint8> bytes;
			bytes.resize(size_t(maxCount) * sizeof(uint32), 0);

			const uint32 count = (uint32)std::min<size_t>(values.size(), maxCount);
			if (count > 0) std::memcpy(bytes.data(), values.data(), size_t(count) * sizeof(uint32));

			renderer.UpdateBuffer(bufferId, std::move(bytes));
		};

		auto uploadF32Table = [&](uint64 bufferId, const std::vector<float>& values, uint32 maxCount)
		{
			std::vector<uint8> bytes;
			bytes.resize(size_t(maxCount) * sizeof(float), 0);

			const uint32 count = (uint32)std::min<size_t>(values.size(), maxCount);
			if (count > 0) std::memcpy(bytes.data(), values.data(), size_t(count) * sizeof(float));

			renderer.UpdateBuffer(bufferId, std::move(bytes));
		};

		auto uploadF32x4Table = [&](uint64 bufferId, const std::vector<float4>& values, uint32 maxCount)
		{
			std::vector<uint8> bytes;
			bytes.resize(size_t(maxCount) * sizeof(float4), 0);

			const uint32 count = (uint32)std::min<size_t>(values.size(), maxCount);
			if (count > 0) std::memcpy(bytes.data(), values.data(), size_t(count) * sizeof(float4));

			renderer.UpdateBuffer(bufferId, std::move(bytes));
		};

		// TypeId -> MeshId
		uploadU32Table(STRING_HASH("Grass_SpeciesLOD0MeshId"), lod0MeshIds, MAX_GRASS_SPECIES);
		uploadU32Table(STRING_HASH("Grass_SpeciesLOD1MeshId"), lod1MeshIds, MAX_GRASS_SPECIES);
		uploadU32Table(STRING_HASH("Grass_SpeciesLOD2MeshId"), lod2MeshIds, MAX_GRASS_SPECIES);

		// Base/Special prefix + id mapping
		{
			std::vector<float> basePrefix;
			basePrefix.reserve(m_BaseSpeciesIds.size());

			float running = 0.0f;
			for (uint32 i = 0u; i < (uint32)m_BaseSpeciesIds.size(); ++i)
			{
				uint32 sp = m_BaseSpeciesIds[i];
				running += std::max(0.0f, m_GrassDescs[sp].Weight);
				basePrefix.push_back(running);
			}
			if (running <= 1e-8f)
			{
				basePrefix.resize(m_BaseSpeciesIds.size());
				for (uint32 i = 0u; i < (uint32)basePrefix.size(); ++i) basePrefix[i] = float(i + 1u);
			}

			std::vector<float> specialPrefix;
			specialPrefix.reserve(m_SpecialSpeciesIds.size());

			running = 0.0f;
			for (uint32 i = 0u; i < (uint32)m_SpecialSpeciesIds.size(); ++i)
			{
				uint32 sp = m_SpecialSpeciesIds[i];
				running += std::max(0.0f, m_GrassDescs[sp].Weight);
				specialPrefix.push_back(running);
			}
			if (!m_SpecialSpeciesIds.empty() && running <= 1e-8f)
			{
				specialPrefix.resize(m_SpecialSpeciesIds.size());
				for (uint32 i = 0u; i < (uint32)specialPrefix.size(); ++i) specialPrefix[i] = float(i + 1u);
			}

			uploadF32Table(STRING_HASH("Grass_BaseSpeciesWeightPrefix"), basePrefix, MAX_GRASS_SPECIES);
			uploadF32Table(STRING_HASH("Grass_SpecialSpeciesWeightPrefix"), specialPrefix, MAX_GRASS_SPECIES);

			uploadU32Table(STRING_HASH("Grass_BaseSpeciesIds"), m_BaseSpeciesIds, MAX_GRASS_SPECIES);
			uploadU32Table(STRING_HASH("Grass_SpecialSpeciesIds"), m_SpecialSpeciesIds, MAX_GRASS_SPECIES);
		}

		// Species variation offset/count & identity mapTypeId
		{
			std::vector<uint32> offsets(m_SpeciesVarOffset);
			std::vector<uint32> counts(m_SpeciesVarCount);

			std::vector<uint32> mapTypeId(numTypes, 0u);
			for (uint32 t = 0u; t < numTypes; ++t) mapTypeId[t] = t;

			uploadU32Table(STRING_HASH("Grass_SpeciesVarOffsets"), offsets, MAX_GRASS_SPECIES + 1u);
			uploadU32Table(STRING_HASH("Grass_SpeciesVarCounts"), counts, MAX_GRASS_SPECIES);
			uploadU32Table(STRING_HASH("Grass_SpeciesVarToTypeId"), mapTypeId, MAX_GRASS_SPECIES);
		}

		// Per-type params
		{
			std::vector<float4> typeParams(numTypes);
			for (uint32 t = 0u; t < numTypes; ++t)
			{
				const uint32 sp = m_TypeToSpecies[t];
				const GrassDesc& gd = m_GrassDescs[sp];

				typeParams[t] = float4(gd.MinScale, gd.MaxScale, gd.BendStrengthMin, gd.BendStrengthMax);
			}

			uploadF32x4Table(STRING_HASH("Grass_TypeParams0"), typeParams, MAX_GRASS_SPECIES);
		}

		// Per-species cluster params (w kept for compatibility)
		{
			std::vector<float4> spParams(numSpecies);
			for (uint32 s = 0u; s < numSpecies; ++s)
			{
				const GrassDesc& gd = m_GrassDescs[s];
				spParams[s] = float4(gd.ClusterStrength, gd.ClusterScale, gd.ClusterJitter, /*unused*/0.0f);
			}
			uploadF32x4Table(STRING_HASH("Grass_SpeciesClusterParams"), spParams, MAX_GRASS_SPECIES);
		}

		// One-time init: VisibleCellTable / PoolChunkCoord / PoolDirty
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
			[this, &renderer, numSpecies, numTypes](RenderPassContext& ctx)
			{
				ASSERT(ctx.pImmediateContext && ctx.pRegistry, "invalid ctx");
				ASSERT(m_pUpdatePoolsCSO && m_pUpdatePoolsSRB, "UpdatePools PSO/SRB not ready");

				IDeviceContext* pContext = ctx.pImmediateContext;

				// Upload constants once per frame
				{
					MapHelper<hlsl::GrassGenConstants> map(
						pContext,
						ctx.pRegistry->GetBuffer(STRING_HASH("GrassGenConstants")),
						MAP_WRITE,
						MAP_FLAG_DISCARD);

					map->NumSpecies = numSpecies;
					map->NumGrassTypes = numTypes;

					map->NumBaseSpecies = (uint32)m_BaseSpeciesIds.size();
					map->NumSpecialSpecies = (uint32)m_SpecialSpeciesIds.size();

					map->ChunkVisibleDim = 2u * m_ChunkHalfExtent;
					map->ChunkHalfExtent = (int)m_ChunkHalfExtent;
					map->SamplesPerChunk = m_SamplesPerChunk;

					map->ChunkSize = m_ChunkSize;
					map->SpawnRadius = m_SpawnRadius;
					map->Jitter = m_Jitter;
					map->SeedSalt = m_SeedSalt;

					map->YOffset = m_YOffset;
					map->NormalAlignStrength = m_NormalAlignStrength;

					map->LOD0Distance = m_LOD0Distance;
					map->LOD1Distance = m_LOD1Distance;
					map->LodHysteresis = m_LodHysteresis;

					map->DensityContrast = m_DensityContrast;
					map->DensityPow = m_DensityPow;

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

				if (auto* v = m_pUpdatePoolsSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "GRASS_GEN_CONSTANTS")) v->Set(renderer.GetBuffer(STRING_HASH("GrassGenConstants")));
				if (auto* v = m_pUpdatePoolsSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_VisibleCellTable")) v->Set(renderer.GetBufferUAV(STRING_HASH("Grass_VisibleCellTable")));
				if (auto* v = m_pUpdatePoolsSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_PoolChunkCoord")) v->Set(renderer.GetBufferUAV(STRING_HASH("Grass_PoolChunkCoord")));
				if (auto* v = m_pUpdatePoolsSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_PoolDirty")) v->Set(renderer.GetBufferUAV(STRING_HASH("Grass_PoolDirty")));
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

				b.DeclareTextureSRVRead(STRING_HASH("InteractionField"));
				b.DeclareBufferCBVRead(STRING_HASH("GrassGenConstants"));
			},
			[this](RenderPassContext& ctx)
			{
				ASSERT(ctx.pImmediateContext && ctx.pRegistry, "invalid ctx");
				ASSERT(m_pFillNewPoolsCSO && m_pFillNewPoolsSRB, "FillNewPools PSO/SRB not ready");

				IDeviceContext* pContext = ctx.pImmediateContext;

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
					{ SHADER_TYPE_COMPUTE, "GRASS_GEN_CONSTANTS",   SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_VisibleCellTable",    SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_PoolDirty",           SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_PoolChunkCoord",      SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_PoolPositions",       SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
				};
				rl.Variables = vars;
				rl.NumVariables = _countof(vars);

				SamplerDesc linearClamp = { FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP };
				SamplerDesc linearWrap = { FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, TEXTURE_ADDRESS_WRAP,  TEXTURE_ADDRESS_WRAP,  TEXTURE_ADDRESS_WRAP };
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

				if (auto* v = m_pFillNewPoolsSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "GRASS_GEN_CONSTANTS")) v->Set(renderer.GetBuffer(STRING_HASH("GrassGenConstants")));
				if (auto* v = m_pFillNewPoolsSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_VisibleCellTable")) v->Set(renderer.GetBufferUAV(STRING_HASH("Grass_VisibleCellTable")));
				if (auto* v = m_pFillNewPoolsSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_PoolDirty")) v->Set(renderer.GetBufferUAV(STRING_HASH("Grass_PoolDirty")));
				if (auto* v = m_pFillNewPoolsSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_PoolChunkCoord")) v->Set(renderer.GetBufferUAV(STRING_HASH("Grass_PoolChunkCoord")));
				if (auto* v = m_pFillNewPoolsSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_PoolPositions")) v->Set(renderer.GetBufferUAV(STRING_HASH("Grass_PoolPositions")));
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
				disp.ThreadGroupCountX = 3;
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

				if (auto* v = m_pClearSpeciesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_SpeciesLodCounts")) v->Set(renderer.GetBufferUAV(STRING_HASH("Grass_SpeciesLodCounts")));
				if (auto* v = m_pClearSpeciesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_SpeciesLodOffsets")) v->Set(renderer.GetBufferUAV(STRING_HASH("Grass_SpeciesLodOffsets")));
				if (auto* v = m_pClearSpeciesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_SpeciesLodWriteCounters")) v->Set(renderer.GetBufferUAV(STRING_HASH("Grass_SpeciesLodWriteCounters")));
			});

		// =====================================================================
		// Pass B-2) CountInstancesFromPools
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

				b.DeclareBufferSRVRead(STRING_HASH("Grass_TypeParams0"));
				b.DeclareBufferSRVRead(STRING_HASH("Grass_SpeciesClusterParams"));

				b.DeclareBufferSRVRead(STRING_HASH("Grass_BaseSpeciesWeightPrefix"));
				b.DeclareBufferSRVRead(STRING_HASH("Grass_BaseSpeciesIds"));
				b.DeclareBufferSRVRead(STRING_HASH("Grass_SpecialSpeciesWeightPrefix"));
				b.DeclareBufferSRVRead(STRING_HASH("Grass_SpecialSpeciesIds"));

				b.DeclareBufferSRVRead(STRING_HASH("Grass_SpeciesVarOffsets"));
				b.DeclareBufferSRVRead(STRING_HASH("Grass_SpeciesVarCounts"));
				b.DeclareBufferSRVRead(STRING_HASH("Grass_SpeciesVarToTypeId"));

				b.DeclareBufferCBVRead(STRING_HASH("GrassGenConstants"));
			},
			[this](RenderPassContext& ctx)
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
					{ SHADER_TYPE_COMPUTE, "GRASS_GEN_CONSTANTS",             SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_VisibleCellTable",              SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_PoolPositions",                 SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_PoolChunkCoord",                SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_SpeciesLodCounts",              SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },

					{ SHADER_TYPE_COMPUTE, "g_TypeParams0",                   SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_SpeciesClusterParams",          SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },

					{ SHADER_TYPE_COMPUTE, "g_BaseSpeciesWeightPrefix",       SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_BaseSpeciesIds",                SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_SpecialSpeciesWeightPrefix",    SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_SpecialSpeciesIds",             SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },

					{ SHADER_TYPE_COMPUTE, "g_SpeciesVarOffsets",             SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_SpeciesVarCounts",              SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_SpeciesVarToTypeId",            SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
				};
				rl.Variables = vars;
				rl.NumVariables = _countof(vars);

				SamplerDesc linearClamp = { FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP };
				SamplerDesc linearWrap = { FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, TEXTURE_ADDRESS_WRAP,  TEXTURE_ADDRESS_WRAP,  TEXTURE_ADDRESS_WRAP };
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

				if (auto* v = m_pCountSpeciesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "GRASS_GEN_CONSTANTS")) v->Set(renderer.GetBuffer(STRING_HASH("GrassGenConstants")));
				if (auto* v = m_pCountSpeciesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_VisibleCellTable")) v->Set(renderer.GetBufferUAV(STRING_HASH("Grass_VisibleCellTable")));
				if (auto* v = m_pCountSpeciesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_PoolPositions")) v->Set(renderer.GetBufferUAV(STRING_HASH("Grass_PoolPositions")));
				if (auto* v = m_pCountSpeciesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_PoolChunkCoord")) v->Set(renderer.GetBufferUAV(STRING_HASH("Grass_PoolChunkCoord")));
				if (auto* v = m_pCountSpeciesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_SpeciesLodCounts")) v->Set(renderer.GetBufferUAV(STRING_HASH("Grass_SpeciesLodCounts")));

				if (auto* v = m_pCountSpeciesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_TypeParams0")) v->Set(renderer.GetBufferSRV(STRING_HASH("Grass_TypeParams0")), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
				if (auto* v = m_pCountSpeciesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_SpeciesClusterParams")) v->Set(renderer.GetBufferSRV(STRING_HASH("Grass_SpeciesClusterParams")), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);

				if (auto* v = m_pCountSpeciesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_BaseSpeciesWeightPrefix")) v->Set(renderer.GetBufferSRV(STRING_HASH("Grass_BaseSpeciesWeightPrefix")), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
				if (auto* v = m_pCountSpeciesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_BaseSpeciesIds")) v->Set(renderer.GetBufferSRV(STRING_HASH("Grass_BaseSpeciesIds")), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
				if (auto* v = m_pCountSpeciesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_SpecialSpeciesWeightPrefix")) v->Set(renderer.GetBufferSRV(STRING_HASH("Grass_SpecialSpeciesWeightPrefix")), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
				if (auto* v = m_pCountSpeciesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_SpecialSpeciesIds")) v->Set(renderer.GetBufferSRV(STRING_HASH("Grass_SpecialSpeciesIds")), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);

				if (auto* v = m_pCountSpeciesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_SpeciesVarOffsets")) v->Set(renderer.GetBufferSRV(STRING_HASH("Grass_SpeciesVarOffsets")), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
				if (auto* v = m_pCountSpeciesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_SpeciesVarCounts")) v->Set(renderer.GetBufferSRV(STRING_HASH("Grass_SpeciesVarCounts")), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
				if (auto* v = m_pCountSpeciesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_SpeciesVarToTypeId")) v->Set(renderer.GetBufferSRV(STRING_HASH("Grass_SpeciesVarToTypeId")), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
			});

		// =====================================================================
		// Pass B-3) PrefixSpeciesOffsets
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

				if (auto* v = m_pPrefixSpeciesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "GRASS_GEN_CONSTANTS")) v->Set(renderer.GetBuffer(STRING_HASH("GrassGenConstants")));
				if (auto* v = m_pPrefixSpeciesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_SpeciesLodCounts")) v->Set(renderer.GetBufferUAV(STRING_HASH("Grass_SpeciesLodCounts")));
				if (auto* v = m_pPrefixSpeciesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_SpeciesLodOffsets")) v->Set(renderer.GetBufferUAV(STRING_HASH("Grass_SpeciesLodOffsets")));
				if (auto* v = m_pPrefixSpeciesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_SpeciesLodWriteCounters")) v->Set(renderer.GetBufferUAV(STRING_HASH("Grass_SpeciesLodWriteCounters")));
			});

		// =====================================================================
		// Pass C) BuildInstancesFromPools
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

				b.DeclareBufferSRVRead(STRING_HASH("Grass_SpeciesLOD0MeshId"));
				b.DeclareBufferSRVRead(STRING_HASH("Grass_SpeciesLOD1MeshId"));
				b.DeclareBufferSRVRead(STRING_HASH("Grass_SpeciesLOD2MeshId"));

				b.DeclareTextureSRVRead(STRING_HASH("InteractionField"));

				b.DeclareBufferSRVRead(STRING_HASH("Grass_TypeParams0"));
				b.DeclareBufferSRVRead(STRING_HASH("Grass_SpeciesClusterParams"));

				b.DeclareBufferSRVRead(STRING_HASH("Grass_BaseSpeciesWeightPrefix"));
				b.DeclareBufferSRVRead(STRING_HASH("Grass_BaseSpeciesIds"));
				b.DeclareBufferSRVRead(STRING_HASH("Grass_SpecialSpeciesWeightPrefix"));
				b.DeclareBufferSRVRead(STRING_HASH("Grass_SpecialSpeciesIds"));

				b.DeclareBufferSRVRead(STRING_HASH("Grass_SpeciesVarOffsets"));
				b.DeclareBufferSRVRead(STRING_HASH("Grass_SpeciesVarCounts"));
				b.DeclareBufferSRVRead(STRING_HASH("Grass_SpeciesVarToTypeId"));

				b.DeclareBufferCBVRead(STRING_HASH("GrassGenConstants"));
			},
			[this](RenderPassContext& ctx)
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
					{ SHADER_TYPE_COMPUTE, "GRASS_GEN_CONSTANTS",       SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_VisibleCellTable",        SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_PoolPositions",           SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_PoolChunkCoord",          SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },

					{ SHADER_TYPE_COMPUTE, "g_SpeciesLodOffsets",       SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_SpeciesLodWriteCounters", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },

					{ SHADER_TYPE_COMPUTE, "g_SpeciesLOD0MeshId",       SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_SpeciesLOD1MeshId",       SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_SpeciesLOD2MeshId",       SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },

					{ SHADER_TYPE_COMPUTE, "g_TypeParams0",             SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_SpeciesClusterParams",    SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },

					{ SHADER_TYPE_COMPUTE, "g_BaseSpeciesWeightPrefix",    SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_BaseSpeciesIds",             SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_SpecialSpeciesWeightPrefix", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_SpecialSpeciesIds",          SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },

					{ SHADER_TYPE_COMPUTE, "g_SpeciesVarOffsets",       SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_SpeciesVarCounts",        SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_SpeciesVarToTypeId",      SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },

					{ SHADER_TYPE_COMPUTE, "g_MeshInstanceCountBuffer", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
					{ SHADER_TYPE_COMPUTE, "g_InteractionField",        SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
				};
				rl.Variables = vars;
				rl.NumVariables = _countof(vars);

				SamplerDesc linearClamp = { FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP };
				SamplerDesc linearWrap = { FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, TEXTURE_ADDRESS_WRAP,  TEXTURE_ADDRESS_WRAP,  TEXTURE_ADDRESS_WRAP };
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

				// Outputs as STATIC
				if (auto* pVar = m_pBuildInstancesCSO->GetStaticVariableByName(SHADER_TYPE_COMPUTE, "g_OutInstancesLOD0")) pVar->Set(renderer.GetBufferUAV(STRING_HASH("GrassInstanceBufferLOD0")));
				if (auto* pVar = m_pBuildInstancesCSO->GetStaticVariableByName(SHADER_TYPE_COMPUTE, "g_OutInstancesLOD1")) pVar->Set(renderer.GetBufferUAV(STRING_HASH("GrassInstanceBufferLOD1")));
				if (auto* pVar = m_pBuildInstancesCSO->GetStaticVariableByName(SHADER_TYPE_COMPUTE, "g_OutInstancesLOD2")) pVar->Set(renderer.GetBufferUAV(STRING_HASH("GrassInstanceBufferLOD2")));

				m_pBuildInstancesCSO->CreateShaderResourceBinding(&m_pBuildInstancesSRB, true);
				ASSERT(m_pBuildInstancesSRB, "BuildInstances SRB create failed");

				if (auto* v = m_pBuildInstancesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "GRASS_GEN_CONSTANTS")) v->Set(renderer.GetBuffer(STRING_HASH("GrassGenConstants")));
				if (auto* v = m_pBuildInstancesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_VisibleCellTable")) v->Set(renderer.GetBufferUAV(STRING_HASH("Grass_VisibleCellTable")));
				if (auto* v = m_pBuildInstancesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_PoolPositions")) v->Set(renderer.GetBufferUAV(STRING_HASH("Grass_PoolPositions")));
				if (auto* v = m_pBuildInstancesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_PoolChunkCoord")) v->Set(renderer.GetBufferUAV(STRING_HASH("Grass_PoolChunkCoord")));

				if (auto* v = m_pBuildInstancesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_SpeciesLodOffsets")) v->Set(renderer.GetBufferUAV(STRING_HASH("Grass_SpeciesLodOffsets")));
				if (auto* v = m_pBuildInstancesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_SpeciesLodWriteCounters")) v->Set(renderer.GetBufferUAV(STRING_HASH("Grass_SpeciesLodWriteCounters")));

				if (auto* v = m_pBuildInstancesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_SpeciesLOD0MeshId")) v->Set(renderer.GetBufferSRV(STRING_HASH("Grass_SpeciesLOD0MeshId")), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
				if (auto* v = m_pBuildInstancesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_SpeciesLOD1MeshId")) v->Set(renderer.GetBufferSRV(STRING_HASH("Grass_SpeciesLOD1MeshId")), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
				if (auto* v = m_pBuildInstancesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_SpeciesLOD2MeshId")) v->Set(renderer.GetBufferSRV(STRING_HASH("Grass_SpeciesLOD2MeshId")), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);

				if (auto* v = m_pBuildInstancesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_TypeParams0")) v->Set(renderer.GetBufferSRV(STRING_HASH("Grass_TypeParams0")), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
				if (auto* v = m_pBuildInstancesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_SpeciesClusterParams")) v->Set(renderer.GetBufferSRV(STRING_HASH("Grass_SpeciesClusterParams")), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);

				if (auto* v = m_pBuildInstancesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_BaseSpeciesWeightPrefix")) v->Set(renderer.GetBufferSRV(STRING_HASH("Grass_BaseSpeciesWeightPrefix")), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
				if (auto* v = m_pBuildInstancesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_BaseSpeciesIds")) v->Set(renderer.GetBufferSRV(STRING_HASH("Grass_BaseSpeciesIds")), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
				if (auto* v = m_pBuildInstancesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_SpecialSpeciesWeightPrefix")) v->Set(renderer.GetBufferSRV(STRING_HASH("Grass_SpecialSpeciesWeightPrefix")), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
				if (auto* v = m_pBuildInstancesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_SpecialSpeciesIds")) v->Set(renderer.GetBufferSRV(STRING_HASH("Grass_SpecialSpeciesIds")), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);

				if (auto* v = m_pBuildInstancesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_SpeciesVarOffsets")) v->Set(renderer.GetBufferSRV(STRING_HASH("Grass_SpeciesVarOffsets")), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
				if (auto* v = m_pBuildInstancesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_SpeciesVarCounts")) v->Set(renderer.GetBufferSRV(STRING_HASH("Grass_SpeciesVarCounts")), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
				if (auto* v = m_pBuildInstancesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_SpeciesVarToTypeId")) v->Set(renderer.GetBufferSRV(STRING_HASH("Grass_SpeciesVarToTypeId")), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);

				if (auto* v = m_pBuildInstancesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_MeshInstanceCountBuffer")) v->Set(renderer.GetBufferUAV(STRING_HASH("IndirectMeshInstanceCountBuffer")));
				if (auto* v = m_pBuildInstancesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_InteractionField")) v->Set(renderer.GetTextureSRV(STRING_HASH("InteractionField")), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
			});
	}

	void GrassSystem::ClearGrassDescs()
	{
		m_GrassDescs.clear();
		m_TypeIndirect.clear();

		m_SpeciesVarOffset.clear();
		m_SpeciesVarCount.clear();
		m_TypeToSpecies.clear();
		m_TypeToVariation.clear();

		m_BaseSpeciesIds.clear();
		m_SpecialSpeciesIds.clear();
	}

	uint32 GrassSystem::AddBaseGrass(const GrassDesc& desc)
	{
		ASSERT(m_GrassDescs.size() < MAX_GRASS_SPECIES, "Max grass species exceeded.");

		GrassDesc d = desc;
		d.IsSpecial = false;

		ASSERT(!d.Variations.empty(), "Base grass must have at least 1 mesh.");
		for (auto* p : d.Variations) ASSERT(p, "Variation mesh is null");

		ASSERT(d.MinScale > 0.0f, "MinScale must be > 0");
		ASSERT(d.MaxScale >= d.MinScale, "MaxScale must be >= MinScale");

		m_GrassDescs.push_back(d);
		return (uint32)(m_GrassDescs.size() - 1u);
	}

	uint32 GrassSystem::AddSpecialGrass(const GrassDesc& desc)
	{
		ASSERT(m_GrassDescs.size() < MAX_GRASS_SPECIES, "Max grass species exceeded.");

		GrassDesc d = desc;
		d.IsSpecial = true;

		ASSERT(!d.Variations.empty(), "Special grass must have at least 1 mesh.");
		for (auto* p : d.Variations) ASSERT(p, "Variation mesh is null");

		ASSERT(d.MinScale > 0.0f, "MinScale must be > 0");
		ASSERT(d.MaxScale >= d.MinScale, "MaxScale must be >= MinScale");

		ASSERT(d.ClusterStrength >= 0.0f, "ClusterStrength must be >= 0");
		ASSERT(d.ClusterScale >= 0.0f, "ClusterScale must be >= 0");
		ASSERT(d.ClusterJitter >= 0.0f, "ClusterJitter must be >= 0");

		m_GrassDescs.push_back(d);
		return (uint32)(m_GrassDescs.size() - 1u);
	}

} // namespace shz