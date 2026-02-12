#include "pch.h"
#include "Engine/RenderSystem/Public/GrassSystem.h"

#include <algorithm>
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

    void GrassSystem::InstallPasses(
        Renderer& renderer,
        RenderScene& scene,
        IndirectArgsSystem& indirect,
        const InteractionSystem& interaction)
    {
        m_pInteractionSystem = &interaction;

        ASSERT(!m_Species.empty(), "GrassSystem: no species. Call AddGrassDesc() at least once.");
        for (const GrassDesc& d : m_Species)
        {
            ASSERT(d.pMeshLod0, "GrassDesc.pMeshLod0 is null");
            ASSERT(d.pCrossMeshLod1, "GrassDesc.pCrossMeshLod1 is null");
            ASSERT(d.pBillboardMeshLod2, "GrassDesc.pBillboardMeshLod2 is null");
        }

        const uint32 visibleDim = 2u * m_ChunkHalfExtent;
        const uint32 visibleCells = visibleDim * visibleDim;
        const uint32 numPools = visibleCells;

        const uint32 numSpecies = static_cast<uint32>(m_Species.size());

        // ---------------------------------------------------------------------
        // Buffers (render instances) - single buffer per LOD
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
        // Species meshId table (uint4 per species: {lod0MeshId,lod1MeshId,lod2MeshId,_pad})
        // ---------------------------------------------------------------------
        {
            BufferDesc bd = {};
            bd.Name = "GrassSpeciesMeshIdTable";
            bd.Usage = USAGE_DYNAMIC;
            bd.BindFlags = BIND_SHADER_RESOURCE;
            bd.CPUAccessFlags = CPU_ACCESS_WRITE;
            bd.Mode = BUFFER_MODE_STRUCTURED;
            bd.ElementByteStride = 16;
            bd.Size = uint64(numSpecies) * 16ull;
            renderer.AddBuffer(STRING_HASH("GrassSpeciesMeshIdTable"), bd);
        }

        // ---------------------------------------------------------------------
        // Allocate indirect meshes per species per LOD
        // - Each species/LOD has its own MeshId so counts/prefix-sum are per mesh.
        // ---------------------------------------------------------------------
        m_SpeciesIndirect.clear();
        m_SpeciesIndirect.resize(numSpecies);

        for (uint32 si = 0; si < numSpecies; ++si)
        {
            const GrassDesc& d = m_Species[si];

            const uint32 lod0Sections = static_cast<uint32>(d.pMeshLod0->Sections.size());
            const uint32 lod1Sections = static_cast<uint32>(d.pCrossMeshLod1->Sections.size());
            const uint32 lod2Sections = static_cast<uint32>(d.pBillboardMeshLod2->Sections.size());

            SpeciesIndirect s = {};
            s.LOD0 = indirect.AllocateMesh("GrassLOD0_Species", lod0Sections);
            s.LOD1 = indirect.AllocateMesh("GrassLOD1_Species", lod1Sections);
            s.LOD2 = indirect.AllocateMesh("GrassLOD2_Species", lod2Sections);

            m_SpeciesIndirect[si] = s;

            auto SetMeshTemplates = [&](const StaticMeshRenderData* mesh, uint32 baseSlot)
            {
                const uint32 sectionCount = static_cast<uint32>(mesh->Sections.size());
                for (uint32 secIndex = 0; secIndex < sectionCount; ++secIndex)
                {
                    const auto& sec = mesh->Sections[secIndex];

                    hlsl::IndirectArgsTemplate t = {};
                    t.IndexCountPerInstance = sec.IndexCount;
                    t.StartIndexLocation = sec.FirstIndex;
                    t.BaseVertexLocation = sec.BaseVertex;
                    t.StartInstanceLocation = 0;

                    indirect.SetTemplate(baseSlot + secIndex, t);
                }
            };

            SetMeshTemplates(d.pMeshLod0, s.LOD0.BaseSlot);
            SetMeshTemplates(d.pCrossMeshLod1, s.LOD1.BaseSlot);
            SetMeshTemplates(d.pBillboardMeshLod2, s.LOD2.BaseSlot);

            // Register indirect objects to scene (per species)
            {
                RenderScene::IndirectObjectDesc od = {};
                od.bCastShadow = true;
                od.PassKey = STRING_HASH("GBuffer");
                od.bDepthPrepass = false;

                od.pMesh = d.pMeshLod0;
                od.IndirectBaseSlot = s.LOD0.BaseSlot;
                od.IndirectMeshId = s.LOD0.MeshId;
                scene.AddIndirect(od);

                od.pMesh = d.pCrossMeshLod1;
                od.IndirectBaseSlot = s.LOD1.BaseSlot;
                od.IndirectMeshId = s.LOD1.MeshId;
                scene.AddIndirect(od);

                od.pMesh = d.pBillboardMeshLod2;
                od.IndirectBaseSlot = s.LOD2.BaseSlot;
                od.IndirectMeshId = s.LOD2.MeshId;
                scene.AddIndirect(od);
            }
        }

        // Upload species mesh id table
        {
            MapHelper<uint4> map(
                renderer.GetImmediateContext(),
                renderer.GetBuffer(STRING_HASH("GrassSpeciesMeshIdTable")),
                MAP_WRITE,
                MAP_FLAG_DISCARD);

            for (uint32 si = 0; si < numSpecies; ++si)
            {
                uint4 v = {};
                v.x = m_SpeciesIndirect[si].LOD0.MeshId;
                v.y = m_SpeciesIndirect[si].LOD1.MeshId;
                v.z = m_SpeciesIndirect[si].LOD2.MeshId;
                v.w = 0;
                map[si] = v;
            }
        }

        // ---------------------------------------------------------------------
        // One-time init of tables
        // ---------------------------------------------------------------------
        {
            const int32 kIntMin = (int32)0x80000000u;

            std::vector<uint8> cells;
            cells.resize(size_t(visibleCells) * 16u, 0);

            for (uint32 i = 0; i < visibleCells; ++i)
            {
                const uint32 poolIndex = i;
                std::memcpy(cells.data() + size_t(i) * 16u + 0u, &poolIndex, sizeof(uint32));

                int32 cc[2] = { kIntMin, kIntMin };
                std::memcpy(cells.data() + size_t(i) * 16u + 4u, cc, 8u);
            }
            renderer.UpdateBuffer(STRING_HASH("Grass_VisibleCellTable"), std::move(cells));

            std::vector<uint8> poolCoord;
            poolCoord.resize(size_t(numPools) * 16u, 0);

            for (uint32 i = 0; i < numPools; ++i)
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

                // Upload constants
                {
                    MapHelper<hlsl::GrassGenConstants> map(
                        pContext,
                        ctx.pRegistry->GetBuffer(STRING_HASH("GrassGenConstants")),
                        MAP_WRITE,
                        MAP_FLAG_DISCARD);

                    map->ChunkVisibleDim = 2u * m_ChunkHalfExtent;
                    map->ChunkHalfExtent = m_ChunkHalfExtent;
                    map->NumPools = map->ChunkVisibleDim * map->ChunkVisibleDim;
                    map->SamplesPerChunk = m_SamplesPerChunk;
                    map->ChunkSize = m_ChunkSize;

                    map->NumSpecies = numSpecies;

                    // Distances: use species[0] as global LOD distances (keep simple for now).
                    map->LOD0Distance = m_Species[0].LOD0Distance;
                    map->LOD1Distance = m_Species[0].LOD1Distance;
                    map->LodHysteresis = m_Species[0].LodHysteresis;

                    map->YOffset = m_YOffset;
                    map->Jitter = m_Jitter;

                    map->MinPitch = m_MinPitch;
                    map->MaxPitch = m_MaxPitch;
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
                    { SHADER_TYPE_COMPUTE, "GRASS_GEN_CONSTANTS",   SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
                    { SHADER_TYPE_COMPUTE, "g_VisibleCellTable",    SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
                    { SHADER_TYPE_COMPUTE, "g_PoolChunkCoord",      SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
                    { SHADER_TYPE_COMPUTE, "g_PoolDirty",           SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
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

                b.DeclareTextureSRVRead(STRING_HASH("GrassDensityField"));
                b.DeclareTextureSRVRead(STRING_HASH("HeightField"));

                b.DeclareBufferCBVRead(STRING_HASH("GrassGenConstants"));
            },
            [this, &renderer](RenderPassContext& ctx)
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
                    { SHADER_TYPE_COMPUTE, "g_HeightField",         SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
                    { SHADER_TYPE_COMPUTE, "g_DensityField",        SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
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
                if (auto* v = m_pFillNewPoolsSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_HeightField"))
                {
                    v->Set(renderer.GetTextureSRV(STRING_HASH("HeightField")), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
                }
                if (auto* v = m_pFillNewPoolsSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_DensityField"))
                {
                    v->Set(renderer.GetTextureSRV(STRING_HASH("GrassDensityField")), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
                }
            });

        // =====================================================================
        // Pass C) CountInstancesFromPools (writes MeshInstanceCountBuffer only)
        // =====================================================================
        renderer.AddPass(
            "Grass_CountInstancesFromPools",
            EPassExecutionDomain::OutsideRenderPass,
            [&](RenderPassBuilder& b)
            {
                b.DeclareBufferUAV(STRING_HASH("Grass_PoolPositions"), RENDER_ACCESS_READ);
                b.DeclareBufferUAV(STRING_HASH("Grass_VisibleCellTable"), RENDER_ACCESS_READ);
                b.DeclareBufferUAV(STRING_HASH("Grass_PoolChunkCoord"), RENDER_ACCESS_READ);

                b.DeclareTextureSRVRead(STRING_HASH("GrassDensityField"));
                b.DeclareTextureSRVRead(STRING_HASH("InteractionField"));
                b.DeclareTextureSRVRead(STRING_HASH("HeightField"));

                b.DeclareBufferUAV(STRING_HASH("IndirectMeshInstanceCountBuffer"), RENDER_ACCESS_WRITE);

                b.DeclareBufferSRVRead(STRING_HASH("GrassSpeciesMeshIdTable"));
                b.DeclareBufferCBVRead(STRING_HASH("GrassGenConstants"));
            },
            [this, &renderer](RenderPassContext& ctx)
            {
                ASSERT(ctx.pImmediateContext && ctx.pRegistry, "invalid ctx");
                ASSERT(m_pCountInstancesCSO && m_pCountInstancesSRB, "CountInstances PSO/SRB not ready");

                IDeviceContext* pContext = ctx.pImmediateContext;

                pContext->SetPipelineState(m_pCountInstancesCSO);
                pContext->CommitShaderResources(m_pCountInstancesSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

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
                psoCI.PSODesc.Name = "PSO_Grass_CountInstancesFromPools";
                psoCI.PSODesc.PipelineType = PIPELINE_TYPE_COMPUTE;

                auto& rl = psoCI.PSODesc.ResourceLayout;
                rl.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

                ShaderResourceVariableDesc vars[] =
                {
                    { SHADER_TYPE_COMPUTE, "GRASS_GEN_CONSTANTS",           SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
                    { SHADER_TYPE_COMPUTE, "g_VisibleCellTable",            SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
                    { SHADER_TYPE_COMPUTE, "g_PoolPositions",               SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
                    { SHADER_TYPE_COMPUTE, "g_PoolChunkCoord",              SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },

                    { SHADER_TYPE_COMPUTE, "g_MeshInstanceCountBuffer",     SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
                    { SHADER_TYPE_COMPUTE, "g_SpeciesMeshIdTable",          SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },

                    { SHADER_TYPE_COMPUTE, "g_DensityField",                SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
                    { SHADER_TYPE_COMPUTE, "g_InteractionField",            SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
                    { SHADER_TYPE_COMPUTE, "g_HeightField",                 SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
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

                m_pCountInstancesCSO = renderer.AcquirePipelineState(psoCI);
                ASSERT(m_pCountInstancesCSO, "AcquireCompute(CountInstancesFromPools) failed");

                m_pCountInstancesCSO->CreateShaderResourceBinding(&m_pCountInstancesSRB, true);
                ASSERT(m_pCountInstancesSRB, "CountInstances SRB create failed");

                if (auto* v = m_pCountInstancesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "GRASS_GEN_CONSTANTS"))
                {
                    v->Set(renderer.GetBuffer(STRING_HASH("GrassGenConstants")));
                }
                if (auto* v = m_pCountInstancesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_VisibleCellTable"))
                {
                    v->Set(renderer.GetBufferUAV(STRING_HASH("Grass_VisibleCellTable")));
                }
                if (auto* v = m_pCountInstancesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_PoolPositions"))
                {
                    v->Set(renderer.GetBufferUAV(STRING_HASH("Grass_PoolPositions")));
                }
                if (auto* v = m_pCountInstancesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_PoolChunkCoord"))
                {
                    v->Set(renderer.GetBufferUAV(STRING_HASH("Grass_PoolChunkCoord")));
                }
                if (auto* v = m_pCountInstancesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_MeshInstanceCountBuffer"))
                {
                    v->Set(renderer.GetBufferUAV(STRING_HASH("IndirectMeshInstanceCountBuffer")));
                }
                if (auto* v = m_pCountInstancesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_SpeciesMeshIdTable"))
                {
                    v->Set(renderer.GetBufferSRV(STRING_HASH("GrassSpeciesMeshIdTable")));
                }
                if (auto* v = m_pCountInstancesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_DensityField"))
                {
                    v->Set(renderer.GetTextureSRV(STRING_HASH("GrassDensityField")), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
                }
                if (auto* v = m_pCountInstancesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_InteractionField"))
                {
                    v->Set(renderer.GetTextureSRV(STRING_HASH("InteractionField")), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
                }
                if (auto* v = m_pCountInstancesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_HeightField"))
                {
                    v->Set(renderer.GetTextureSRV(STRING_HASH("HeightField")), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
                }
            });

        // =====================================================================
        // Pass D) BuildInstancesFromPools (uses offsets + scatter counters)
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

                b.DeclareBufferUAV(STRING_HASH("IndirectMeshInstanceOffsetBuffer"), RENDER_ACCESS_READ);
                b.DeclareBufferUAV(STRING_HASH("IndirectMeshScatterCounterBuffer"), RENDER_ACCESS_WRITE);

                b.DeclareTextureSRVRead(STRING_HASH("GrassDensityField"));
                b.DeclareTextureSRVRead(STRING_HASH("InteractionField"));
                b.DeclareTextureSRVRead(STRING_HASH("HeightField"));

                b.DeclareBufferSRVRead(STRING_HASH("GrassSpeciesMeshIdTable"));
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
                    { SHADER_TYPE_COMPUTE, "GRASS_GEN_CONSTANTS",            SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
                    { SHADER_TYPE_COMPUTE, "g_VisibleCellTable",             SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
                    { SHADER_TYPE_COMPUTE, "g_PoolPositions",                SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
                    { SHADER_TYPE_COMPUTE, "g_PoolChunkCoord",               SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },

                    { SHADER_TYPE_COMPUTE, "g_MeshInstanceOffsetBuffer",     SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
                    { SHADER_TYPE_COMPUTE, "g_MeshScatterCounterBuffer",     SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
                    { SHADER_TYPE_COMPUTE, "g_SpeciesMeshIdTable",           SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },

                    { SHADER_TYPE_COMPUTE, "g_DensityField",                 SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
                    { SHADER_TYPE_COMPUTE, "g_InteractionField",             SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
                    { SHADER_TYPE_COMPUTE, "g_HeightField",                  SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
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
                if (auto* v = m_pBuildInstancesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_MeshInstanceOffsetBuffer"))
                {
                    v->Set(renderer.GetBufferUAV(STRING_HASH("IndirectMeshInstanceOffsetBuffer")));
                }
                if (auto* v = m_pBuildInstancesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_MeshScatterCounterBuffer"))
                {
                    v->Set(renderer.GetBufferUAV(STRING_HASH("IndirectMeshScatterCounterBuffer")));
                }
                if (auto* v = m_pBuildInstancesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_SpeciesMeshIdTable"))
                {
                    v->Set(renderer.GetBufferSRV(STRING_HASH("GrassSpeciesMeshIdTable")));
                }
                if (auto* v = m_pBuildInstancesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_DensityField"))
                {
                    v->Set(renderer.GetTextureSRV(STRING_HASH("GrassDensityField")), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
                }
                if (auto* v = m_pBuildInstancesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_InteractionField"))
                {
                    v->Set(renderer.GetTextureSRV(STRING_HASH("InteractionField")), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
                }
                if (auto* v = m_pBuildInstancesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_HeightField"))
                {
                    v->Set(renderer.GetTextureSRV(STRING_HASH("HeightField")), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
                }
            });
    }
} // namespace shz
