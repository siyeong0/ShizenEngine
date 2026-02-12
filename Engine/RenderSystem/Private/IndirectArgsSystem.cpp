#include "pch.h"
#include "Engine/RenderSystem/Public/IndirectArgsSystem.h"

#include <algorithm>
#include <cstring>

#include "Engine/Renderer/Public/Renderer.h"
#include "Engine/Renderer/Public/RenderPassContext.h"
#include "Engine/Renderer/Public/RenderResourceRegistry.h"

#include "Engine/GraphicsTools/Public/MapHelper.hpp"
#include "Engine/RHI/Interface/GraphicsTypes.h"

namespace shz
{
    namespace hlsl
    {
#include "Shaders/HLSL_Structures.hlsli"
    }

    static inline uint32 DivUp(uint32 x, uint32 d)
    {
        return (x + d - 1u) / d;
    }

    bool IndirectArgsSystem::allocateContiguousSlots(uint32 numSlots, uint32& outBaseSlot)
    {
        ASSERT(numSlots > 0 && numSlots < MAX_NUM_INDIRECTS, "Invalid slot count.");

        uint32 run = 0;
        uint32 start = 0;

        for (uint32 i = 0; i < MAX_NUM_INDIRECTS; ++i)
        {
            if (m_SlotUsed[i] == 0)
            {
                if (run == 0)
                {
                    start = i;
                }

                run += 1;

                if (run >= numSlots)
                {
                    outBaseSlot = start;

                    for (uint32 s = 0; s < numSlots; ++s)
                    {
                        m_SlotUsed[start + s] = 1;
                    }

                    m_NumSlots = std::max(m_NumSlots, start + numSlots);
                    return true;
                }
            }
            else
            {
                run = 0;
            }
        }

        return false;
    }

    void IndirectArgsSystem::InstallPasses(Renderer& renderer)
    {
        // ---------------------------------------------------------------------
        // Buffers
        // ---------------------------------------------------------------------

        // Indirect args (RAW 20 bytes per slot)
        {
            BufferDesc bd = {};
            bd.Name = "IndirectArgsBuffer";
            bd.Usage = USAGE_DEFAULT;
            bd.BindFlags = BIND_UNORDERED_ACCESS | BIND_INDIRECT_DRAW_ARGS;
            bd.Mode = BUFFER_MODE_RAW;
            bd.Size = 20u * MAX_NUM_INDIRECTS;

            renderer.AddBuffer(STRING_HASH("IndirectArgsBuffer"), bd);
        }

        // Slot draw-count buffer (RAW 4 bytes per slot) used as ExecuteIndirect CountBuffer
        {
            BufferDesc bd = {};
            bd.Name = "IndirectDrawCountBuffer";
            bd.Usage = USAGE_DEFAULT;
            bd.BindFlags = BIND_UNORDERED_ACCESS | BIND_INDIRECT_DRAW_ARGS;
            bd.Mode = BUFFER_MODE_RAW;
            bd.Size = 4u * MAX_NUM_INDIRECTS;

            renderer.AddBuffer(STRING_HASH("IndirectDrawCountBuffer"), bd);
        }

        // Mesh instance-count buffer (RAW 4 bytes per mesh). Other compute kernels atomic-add here.
        {
            BufferDesc bd = {};
            bd.Name = "IndirectMeshInstanceCountBuffer";
            bd.Usage = USAGE_DEFAULT;
            bd.BindFlags = BIND_UNORDERED_ACCESS;
            bd.Mode = BUFFER_MODE_RAW;
            bd.Size = 4u * MAX_NUM_INDIRECT_MESHES;

            renderer.AddBuffer(STRING_HASH("IndirectMeshInstanceCountBuffer"), bd);
        }

        // Header CB (small, stable layout)
        {
            BufferDesc bd = {};
            bd.Name = "IndirectArgsHeaderCB";
            bd.Usage = USAGE_DYNAMIC;
            bd.BindFlags = BIND_UNIFORM_BUFFER;
            bd.CPUAccessFlags = CPU_ACCESS_WRITE;
            bd.Size = sizeof(hlsl::IndirectArgsHeader);

            renderer.AddBuffer(STRING_HASH("IndirectArgsHeaderCB"), bd);
        }

        // Templates SRV (StructuredBuffer<IndirectArgsTemplate>)
        {
            BufferDesc bd = {};
            bd.Name = "IndirectArgsTemplatesBuffer";
            bd.Usage = USAGE_DYNAMIC;
            bd.BindFlags = BIND_SHADER_RESOURCE;
            bd.CPUAccessFlags = CPU_ACCESS_WRITE;
            bd.Mode = BUFFER_MODE_STRUCTURED;
            bd.ElementByteStride = sizeof(hlsl::IndirectArgsTemplate);
            bd.Size = sizeof(hlsl::IndirectArgsTemplate) * MAX_NUM_INDIRECTS;

            renderer.AddBuffer(STRING_HASH("IndirectArgsTemplatesBuffer"), bd);
        }

        // SlotMeshId SRV (StructuredBuffer<uint>)
        {
            BufferDesc bd = {};
            bd.Name = "IndirectSlotMeshIdBuffer";
            bd.Usage = USAGE_DYNAMIC;
            bd.BindFlags = BIND_SHADER_RESOURCE;
            bd.CPUAccessFlags = CPU_ACCESS_WRITE;
            bd.Mode = BUFFER_MODE_STRUCTURED;
            bd.ElementByteStride = 4u;
            bd.Size = 4u * MAX_NUM_INDIRECTS;

            renderer.AddBuffer(STRING_HASH("IndirectSlotMeshIdBuffer"), bd);
        }

        // ---------------------------------------------------------------------
        // Pass: Write args + slot drawCount
        // ---------------------------------------------------------------------
        renderer.AddPass(
            "IndirectWriteArgs",
            EPassExecutionDomain::OutsideRenderPass,
            [&](RenderPassBuilder& b)
            {
                b.DeclareBufferUAV(STRING_HASH("IndirectArgsBuffer"), RENDER_ACCESS_WRITE);
                b.DeclareBufferUAV(STRING_HASH("IndirectDrawCountBuffer"), RENDER_ACCESS_WRITE);

                b.DeclareBufferUAV(STRING_HASH("IndirectMeshInstanceCountBuffer"), RENDER_ACCESS_READ);

                b.DeclareBufferCBVRead(STRING_HASH("IndirectArgsHeaderCB"));
                b.DeclareBufferSRVRead(STRING_HASH("IndirectArgsTemplatesBuffer"));
                b.DeclareBufferSRVRead(STRING_HASH("IndirectSlotMeshIdBuffer"));
            },
            [this](RenderPassContext& ctx)
            {
                ASSERT(ctx.pImmediateContext, "ImmediateContext is null.");
                ASSERT(ctx.pRegistry, "Registry is null.");
                ASSERT(m_pWriteArgsCSO && m_pWriteArgsSRB, "IndirectWriteArgs PSO/SRB not ready.");

                IDeviceContext* pContext = ctx.pImmediateContext;

                const uint32 numSlots = std::min<uint32>(m_NumSlots, MAX_NUM_INDIRECTS);
                const uint32 numMeshes = std::min<uint32>(m_NumMeshes, MAX_NUM_INDIRECT_MESHES);

                // (0) Update header CB
                {
                    MapHelper<hlsl::IndirectArgsHeader> cb(
                        pContext,
                        ctx.pRegistry->GetBuffer(STRING_HASH("IndirectArgsHeaderCB")),
                        MAP_WRITE,
                        MAP_FLAG_DISCARD);

                    cb->NumSlots = numSlots;
                    cb->NumMeshes = numMeshes;
                    cb->MaxInstances = 1u << 24;
                    cb->_pad0 = 0;
                }

                // (1) Update Templates SRV buffer
                {
                    MapHelper<hlsl::IndirectArgsTemplate> map(
                        pContext,
                        ctx.pRegistry->GetBuffer(STRING_HASH("IndirectArgsTemplatesBuffer")),
                        MAP_WRITE,
                        MAP_FLAG_DISCARD);

                    for (uint32 i = 0; i < numSlots; ++i)
                    {
                        map[i] = m_Templates[i];
                    }

                    for (uint32 i = numSlots; i < MAX_NUM_INDIRECTS; ++i)
                    {
                        hlsl::IndirectArgsTemplate z = {};
                        map[i] = z;
                    }
                }

                // (2) Update SlotMeshId SRV buffer
                {
                    MapHelper<uint32> map(
                        pContext,
                        ctx.pRegistry->GetBuffer(STRING_HASH("IndirectSlotMeshIdBuffer")),
                        MAP_WRITE,
                        MAP_FLAG_DISCARD);

                    for (uint32 i = 0; i < numSlots; ++i)
                    {
                        map[i] = m_SlotMeshId[i];
                    }

                    for (uint32 i = numSlots; i < MAX_NUM_INDIRECTS; ++i)
                    {
                        map[i] = 0;
                    }
                }

                // (3) Dispatch per slot
                pContext->SetPipelineState(m_pWriteArgsCSO);
                pContext->CommitShaderResources(m_pWriteArgsSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

                DispatchComputeAttribs disp = {};
                disp.ThreadGroupCountX = std::max(1u, DivUp(numSlots, 64u));
                disp.ThreadGroupCountY = 1;
                disp.ThreadGroupCountZ = 1;
                pContext->DispatchCompute(disp);
            },
                [this, &renderer]()
            {
                ShaderCreateInfo csCI = {};
                csCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
                csCI.EntryPoint = "WriteIndirectArgs";
                csCI.Desc.Name = "WriteIndirectArgsCS";
                csCI.Desc.ShaderType = SHADER_TYPE_COMPUTE;
                csCI.Desc.UseCombinedTextureSamplers = false;
                csCI.FilePath = m_WriteArgsCS.c_str();

                RefCntAutoPtr<IShader> cs;
                renderer.CreateShader(csCI, &cs);
                ASSERT(cs, "WriteIndirectArgsCS compile failed.");

                ComputePipelineStateCreateInfo psoCI = {};
                psoCI.PSODesc.Name = "PSO_IndirectWriteArgs";
                psoCI.PSODesc.PipelineType = PIPELINE_TYPE_COMPUTE;

                auto& rl = psoCI.PSODesc.ResourceLayout;
                rl.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

                ShaderResourceVariableDesc vars[] =
                {
                    { SHADER_TYPE_COMPUTE, "INDIRECT_ARGS_WRITER_CB",    SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
                    { SHADER_TYPE_COMPUTE, "g_SlotMeshId",              SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
                    { SHADER_TYPE_COMPUTE, "g_Templates",               SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },

                    { SHADER_TYPE_COMPUTE, "g_IndirectArgs",             SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
                    { SHADER_TYPE_COMPUTE, "g_DrawCountBuffer",          SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
                    { SHADER_TYPE_COMPUTE, "g_MeshInstanceCountBuffer",  SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
                };
                rl.Variables = vars;
                rl.NumVariables = _countof(vars);

                psoCI.pCS = cs;

                m_pWriteArgsCSO = renderer.AcquirePipelineState(psoCI, true);
                ASSERT(m_pWriteArgsCSO, "AcquireCompute(IndirectWriteArgs) failed.");

                m_pWriteArgsCSO->CreateShaderResourceBinding(&m_pWriteArgsSRB, true);
                ASSERT(m_pWriteArgsSRB, "IndirectWriteArgs SRB create failed.");

                if (auto* var = m_pWriteArgsSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "INDIRECT_ARGS_WRITER_CB"))
                {
                    var->Set(renderer.GetBuffer(STRING_HASH("IndirectArgsHeaderCB")));
                }

                if (auto* var = m_pWriteArgsSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_SlotMeshId"))
                {
                    var->Set(renderer.GetBufferSRV(STRING_HASH("IndirectSlotMeshIdBuffer")));
                }

                if (auto* var = m_pWriteArgsSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_Templates"))
                {
                    var->Set(renderer.GetBufferSRV(STRING_HASH("IndirectArgsTemplatesBuffer")));
                }

                if (auto* var = m_pWriteArgsSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_IndirectArgs"))
                {
                    var->Set(renderer.GetBufferUAV(STRING_HASH("IndirectArgsBuffer")));
                }

                if (auto* var = m_pWriteArgsSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_DrawCountBuffer"))
                {
                    var->Set(renderer.GetBufferUAV(STRING_HASH("IndirectDrawCountBuffer")));
                }

                if (auto* var = m_pWriteArgsSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_MeshInstanceCountBuffer"))
                {
                    var->Set(renderer.GetBufferUAV(STRING_HASH("IndirectMeshInstanceCountBuffer")));
                }
            });

        // ---------------------------------------------------------------------
        // Pass: Reset mesh instance counts (per mesh)
        // ---------------------------------------------------------------------
        renderer.AddPass(
            "IndirectResetMeshCounts",
            EPassExecutionDomain::OutsideRenderPass,
            [&](RenderPassBuilder& b)
            {
                b.DeclareBufferUAV(STRING_HASH("IndirectMeshInstanceCountBuffer"), RENDER_ACCESS_WRITE);
                b.DeclareBufferCBVRead(STRING_HASH("IndirectArgsHeaderCB"));
            },
            [this](RenderPassContext& ctx)
            {
                ASSERT(ctx.pImmediateContext, "ImmediateContext is null.");
                ASSERT(ctx.pRegistry, "Registry is null.");
                ASSERT(m_pResetMeshCountsCSO && m_pResetMeshCountsSRB, "ResetMeshCounts PSO/SRB not ready.");

                IDeviceContext* pContext = ctx.pImmediateContext;

                const uint32 numMeshes = std::min<uint32>(m_NumMeshes, MAX_NUM_INDIRECT_MESHES);

                // Keep header CB valid even if this pass runs alone.
                {
                    MapHelper<hlsl::IndirectArgsHeader> cb(
                        pContext,
                        ctx.pRegistry->GetBuffer(STRING_HASH("IndirectArgsHeaderCB")),
                        MAP_WRITE,
                        MAP_FLAG_DISCARD);

                    cb->NumSlots = std::min<uint32>(m_NumSlots, MAX_NUM_INDIRECTS);
                    cb->NumMeshes = numMeshes;
                    cb->MaxInstances = 1u << 24;
                    cb->_pad0 = 0;
                }

                pContext->SetPipelineState(m_pResetMeshCountsCSO);
                pContext->CommitShaderResources(m_pResetMeshCountsSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

                DispatchComputeAttribs disp = {};
                disp.ThreadGroupCountX = std::max(1u, DivUp(numMeshes, 64u));
                disp.ThreadGroupCountY = 1;
                disp.ThreadGroupCountZ = 1;
                pContext->DispatchCompute(disp);
            },
                [this, &renderer]()
            {
                ShaderCreateInfo csCI = {};
                csCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
                csCI.EntryPoint = "ResetMeshInstanceCounts";
                csCI.Desc.Name = "ResetMeshInstanceCountsCS";
                csCI.Desc.ShaderType = SHADER_TYPE_COMPUTE;
                csCI.Desc.UseCombinedTextureSamplers = false;
                csCI.FilePath = m_WriteArgsCS.c_str();

                RefCntAutoPtr<IShader> cs;
                renderer.CreateShader(csCI, &cs);
                ASSERT(cs, "ResetMeshInstanceCountsCS compile failed.");

                ComputePipelineStateCreateInfo psoCI = {};
                psoCI.PSODesc.Name = "PSO_IndirectResetMeshCounts";
                psoCI.PSODesc.PipelineType = PIPELINE_TYPE_COMPUTE;

                auto& rl = psoCI.PSODesc.ResourceLayout;
                rl.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

                ShaderResourceVariableDesc vars[] =
                {
                    { SHADER_TYPE_COMPUTE, "INDIRECT_ARGS_WRITER_CB",    SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
                    { SHADER_TYPE_COMPUTE, "g_MeshInstanceCountBuffer",  SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
                };
                rl.Variables = vars;
                rl.NumVariables = _countof(vars);

                psoCI.pCS = cs;

                m_pResetMeshCountsCSO = renderer.AcquirePipelineState(psoCI, true);
                ASSERT(m_pResetMeshCountsCSO, "AcquireCompute(ResetMeshCounts) failed.");

                m_pResetMeshCountsCSO->CreateShaderResourceBinding(&m_pResetMeshCountsSRB, true);
                ASSERT(m_pResetMeshCountsSRB, "ResetMeshCounts SRB create failed.");

                if (auto* var = m_pResetMeshCountsSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "INDIRECT_ARGS_WRITER_CB"))
                {
                    var->Set(renderer.GetBuffer(STRING_HASH("IndirectArgsHeaderCB")));
                }

                if (auto* var = m_pResetMeshCountsSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_MeshInstanceCountBuffer"))
                {
                    var->Set(renderer.GetBufferUAV(STRING_HASH("IndirectMeshInstanceCountBuffer")));
                }
            });
    }

    IndirectArgsSystem::MeshHandle IndirectArgsSystem::AllocateMesh(const std::string& debugName, uint32 numSlots)
    {
        (void)debugName;

        uint32 meshId = UINT32_MAX;

        for (uint32 i = 0; i < MAX_NUM_INDIRECT_MESHES; ++i)
        {
            if (m_MeshUsed[i] == 0)
            {
                m_MeshUsed[i] = 1;
                meshId = i;
                m_NumMeshes = std::max(m_NumMeshes, i + 1);
                break;
            }
        }

        ASSERT(meshId != UINT32_MAX, "AllocateMesh failed: MAX_NUM_INDIRECT_MESHES exhausted.");

        uint32 baseSlot = 0;
        const bool ok = allocateContiguousSlots(numSlots, baseSlot);
        ASSERT(ok, "AllocateMesh failed: cannot allocate contiguous slots. numSlots=%u", numSlots);

        for (uint32 s = 0; s < numSlots; ++s)
        {
            m_SlotMeshId[baseSlot + s] = meshId;
        }

        MeshHandle h = {};
        h.MeshId = meshId;
        h.BaseSlot = baseSlot;
        h.NumSlots = numSlots;
        return h;
    }

    uint32 IndirectArgsSystem::AllocateSlot(const std::string& debugName)
    {
        MeshHandle h = AllocateMesh(debugName, 1);
        return h.BaseSlot;
    }

    void IndirectArgsSystem::ResetAllSlots()
    {
        m_SlotUsed.fill(0);
        m_MeshUsed.fill(0);

        m_NumSlots = 0;
        m_NumMeshes = 0;

        std::memset(m_Templates.data(), 0, sizeof(m_Templates));
        std::memset(m_SlotMeshId.data(), 0, sizeof(m_SlotMeshId));
    }

    void IndirectArgsSystem::SetTemplate(uint32 slot, const hlsl::IndirectArgsTemplate& t)
    {
        ASSERT(slot < MAX_NUM_INDIRECTS, "slot out of range.");
        m_Templates[slot] = t;
    }
} // namespace shz
