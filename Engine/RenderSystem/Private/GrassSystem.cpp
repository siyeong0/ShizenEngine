#include "pch.h"
#include "Engine/RenderSystem/Public/GrassSystem.h"

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

        ASSERT(m_GrassDesc.pMeshLod0, "GrassDesc.pMeshLod0 is null");
        ASSERT(m_GrassDesc.pCrossMeshLod1, "GrassDesc.pCrossMeshLod1 is null");
        ASSERT(m_GrassDesc.pBillboardMeshLod2, "GrassDesc.pBillboardMeshLod2 is null");

        const uint32 visibleDim = 2u * m_ChunkHalfExtent;      // 128
        const uint32 visibleCells = visibleDim * visibleDim;   // 16384
        const uint32 numPools = visibleCells;                  // 1:1 (poolIndex == cellIndex)

        // ---------------------------------------------------------------------
        // Buffers (render instances)
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
        // ChunkPool buffers (FreeList removed)
        //
        // VisibleCellTable : cellIndex -> { poolIndex (=cellIndex), chunkCoord }
        // PoolChunkCoord   : poolIndex -> { chunkCoord, chunkHeight }
        // PoolDirty        : poolIndex -> uint (0/1) (used by Update/Fill only)
        // PoolPositions    : poolIndex * SamplesPerChunk -> float4(x,y,z,metaPacked)
        // ---------------------------------------------------------------------
        {
            BufferDesc bd = {};
            bd.Usage = USAGE_DEFAULT;
            bd.BindFlags = BIND_SHADER_RESOURCE | BIND_UNORDERED_ACCESS;
            bd.Mode = BUFFER_MODE_STRUCTURED;

            // VisibleCellTable: 16B stride
            bd.ElementByteStride = 16;
            bd.Size = uint64(visibleCells) * 16ull;
            bd.Name = "Grass_VisibleCellTable";
            renderer.AddBuffer(STRING_HASH("Grass_VisibleCellTable"), bd);

            // PoolChunkCoord: 16B stride
            bd.ElementByteStride = 16;
            bd.Size = uint64(numPools) * 16ull;
            bd.Name = "Grass_PoolChunkCoord";
            renderer.AddBuffer(STRING_HASH("Grass_PoolChunkCoord"), bd);

            // PoolDirty: 16B stride (uint + padding)
            bd.ElementByteStride = 16;
            bd.Size = uint64(numPools) * 16ull;
            bd.Name = "Grass_PoolDirty";
            renderer.AddBuffer(STRING_HASH("Grass_PoolDirty"), bd);

            // PoolPositions: float4 per sample
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

        // Allocate indirect slots
        m_IndirectSlotLOD0 = indirect.AllocateSlot("GrassLOD0");
        m_IndirectSlotLOD1 = indirect.AllocateSlot("GrassLOD1");
        m_IndirectSlotLOD2 = indirect.AllocateSlot("GrassLOD2");

        // Indirect templates
        {
            hlsl::IndirectArgsTemplate t = {};
            t.StartIndexLocation = 0;
            t.BaseVertexLocation = 0;
            t.StartInstanceLocation = 0;

            t.IndexCountPerInstance = m_GrassDesc.pMeshLod0->IndexCount;
            indirect.SetTemplate(m_IndirectSlotLOD0, t);

            t.IndexCountPerInstance = m_GrassDesc.pCrossMeshLod1->IndexCount;
            indirect.SetTemplate(m_IndirectSlotLOD1, t);

            t.IndexCountPerInstance = m_GrassDesc.pBillboardMeshLod2->IndexCount;
            indirect.SetTemplate(m_IndirectSlotLOD2, t);
        }

        // Register indirect objects to RenderScene
        {
            RenderScene::IndirectObjectDesc d = {};
            d.bCastShadow = true;
            d.PassKey = STRING_HASH("GBuffer");

            d.bDepthPrepass = false;
            d.pMesh = m_GrassDesc.pMeshLod0;
            d.IndirectSlot = m_IndirectSlotLOD0;
            scene.AddIndirect(d);

            d.bDepthPrepass = false;
            d.pMesh = m_GrassDesc.pCrossMeshLod1;
            d.IndirectSlot = m_IndirectSlotLOD1;
            scene.AddIndirect(d);

            d.bDepthPrepass = false;
            d.pMesh = m_GrassDesc.pBillboardMeshLod2;
            d.IndirectSlot = m_IndirectSlotLOD2;
            scene.AddIndirect(d);
        }

        // ---------------------------------------------------------------------
        // One-time init of tables using UpdateBuffer (CPU)
        //
        // - VisibleCellTable: PoolIndex = cellIndex, ChunkCoord = INT_MIN (forces first Update to mark dirty)
        // - PoolChunkCoord  : invalid ChunkCoord, ChunkHeight = 0
        // - PoolDirty       : 0
        // ---------------------------------------------------------------------
        {
            const int32 kIntMin = (int32)0x80000000u;

            // VisibleCellTable: { uint PoolIndex; int2 ChunkCoord; uint _pad; }
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

            // PoolChunkCoord: { uint _pad0; int2 ChunkCoord; float ChunkHeight; }
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

            // PoolDirty: { uint Dirty; uint3 _pad; } -> all zeros
            std::vector<uint8> dirty;
            dirty.resize(size_t(numPools) * 16u, 0);
            renderer.UpdateBuffer(STRING_HASH("Grass_PoolDirty"), std::move(dirty));
        }

        // =====================================================================
        // Pass A) UpdateChunkPools (poolIndex == cellIndex, dirty mark only)
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
            [this, &renderer](RenderPassContext& ctx)
            {
                ASSERT(ctx.pImmediateContext && ctx.pRegistry, "invalid ctx");
                ASSERT(m_pUpdatePoolsCSO && m_pUpdatePoolsSRB, "UpdatePools PSO/SRB not ready");

                IDeviceContext* pContext = ctx.pImmediateContext;

                // Upload constants (includes interaction ring settings).
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

                    map->IndirectSlotLOD0 = m_IndirectSlotLOD0;
                    map->IndirectSlotLOD1 = m_IndirectSlotLOD1;
                    map->IndirectSlotLOD2 = m_IndirectSlotLOD2;

                    map->LOD0Distance = m_GrassDesc.LOD0Distance;
                    map->LOD1Distance = m_GrassDesc.LOD1Distance;

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

                    map->DensityTiling = m_DensityTiling;
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
        // Pass B) FillNewPools (dirty pools only; fills full SamplesPerChunk)
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
                b.DeclareTextureSRVRead(STRING_HASH("InteractionField"));
                b.DeclareTextureSRVRead(STRING_HASH("HeightField"));

                b.DeclareBufferCBVRead(STRING_HASH("GrassGenConstants"));
            },
            [this, &renderer](RenderPassContext& ctx)
            {
                ASSERT(ctx.pImmediateContext && ctx.pRegistry, "invalid ctx");
                ASSERT(m_pFillNewPoolsCSO && m_pFillNewPoolsSRB, "FillNewPools PSO/SRB not ready");

                IDeviceContext* pContext = ctx.pImmediateContext;

                // HeightField must be SRV.
                {
                    StateTransitionDesc tr =
                    {
                        renderer.GetTexture(STRING_HASH("HeightField")),
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
                    { SHADER_TYPE_COMPUTE, "GRASS_GEN_CONSTANTS",   SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
                    { SHADER_TYPE_COMPUTE, "g_VisibleCellTable",    SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
                    { SHADER_TYPE_COMPUTE, "g_PoolDirty",           SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
                    { SHADER_TYPE_COMPUTE, "g_PoolChunkCoord",      SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
                    { SHADER_TYPE_COMPUTE, "g_PoolPositions",       SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
                    { SHADER_TYPE_COMPUTE, "g_HeightField",         SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
                    { SHADER_TYPE_COMPUTE, "g_DensityField",        SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
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
                if (auto* v = m_pFillNewPoolsSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_InteractionField"))
                {
                    v->Set(renderer.GetTextureSRV(STRING_HASH("InteractionField")), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
                }
            });

        // =====================================================================
        // Pass C) BuildInstancesFromPools (every frame)
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

                b.DeclareBufferUAV(STRING_HASH("IndirectCountBuffer"), RENDER_ACCESS_WRITE);

                b.DeclareTextureSRVRead(STRING_HASH("GrassDensityField"));

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
                    { SHADER_TYPE_COMPUTE, "GRASS_GEN_CONSTANTS",   SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
                    { SHADER_TYPE_COMPUTE, "g_VisibleCellTable",    SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
                    { SHADER_TYPE_COMPUTE, "g_PoolPositions",       SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
                    { SHADER_TYPE_COMPUTE, "g_PoolChunkCoord",      SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
                    { SHADER_TYPE_COMPUTE, "g_CounterBuffer",       SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
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
                if (auto* v = m_pBuildInstancesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_CounterBuffer"))
                {
                    v->Set(renderer.GetBufferUAV(STRING_HASH("IndirectCountBuffer")));
                }
                if (auto* v = m_pBuildInstancesSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_DensityField"))
                {
                    v->Set(renderer.GetTextureSRV(STRING_HASH("GrassDensityField")), SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
                }
            });
    }

} // namespace shz
