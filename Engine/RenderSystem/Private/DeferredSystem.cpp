#include "pch.h"
#include "Engine/RenderSystem/Public/DeferredSystem.h"

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

    // -----------------------------------------------------------------------------------------
    // NOTE:
    // - 프로젝트에 맞게 "Depth 전용 PSO/SRB"를 꺼내오는 부분만 맞춰주면 됨.
    // - alpha-test(풀/나뭇잎)는 DepthPrePass에서 discard가 동일하게 일어나야 해서
    //   보통 DepthOnly 전용 셰이더(또는 preprocessor)로 동일한 alpha-cutoff 로직을 넣음.
    // -----------------------------------------------------------------------------------------
    static inline IPipelineState* GetDepthPSO(const DrawPacket& pkt)
    {
        // TODO: 네 DrawPacket 구조에 맞게 수정
        // 예) return pkt.DepthPSO ? pkt.DepthPSO : pkt.PSO;
        return pkt.PSO;
    }

    static inline IShaderResourceBinding* GetDepthSRB(const DrawPacket& pkt)
    {
        // TODO: 네 DrawPacket 구조에 맞게 수정
        // 예) return pkt.DepthSRB ? pkt.DepthSRB : pkt.SRB;
        return pkt.SRB;
    }

    static inline IPipelineState* GetDepthPSO(const DrawIndirectPacket& pkt)
    {
        // TODO: 네 DrawIndirectPacket 구조에 맞게 수정
        return pkt.PSO;
    }

    static inline IShaderResourceBinding* GetDepthSRB(const DrawIndirectPacket& pkt)
    {
        // TODO: 네 DrawIndirectPacket 구조에 맞게 수정
        return pkt.SRB;
    }

    void DeferredSystem::InstallPasses(Renderer& renderer)
    {
        // ------------------------------------------------------------
        // Pass: DepthPrePass (writes GBufferDepth only)
        // ------------------------------------------------------------
        renderer.AddPass(
            "DepthPrepass",
            EPassExecutionDomain::RenderPass,
            [](RenderPassBuilder& b)
            {
                const uint64 kDepth = STRING_HASH("GBufferDepth");

                // Depth-only
                b.DeclareTextureDSVWrite(kDepth);

                // If you want to use indirect for some geometry in depth-only, declare it
                b.DeclareBufferIndirectArgsRead(STRING_HASH("IndirectArgsBuffer"));

                // Grass instances (if depth-only pass needs them via SRV in VS)
                b.DeclareBufferSRVRead(STRING_HASH("GrassInstanceBufferLOD0"));
                b.DeclareBufferSRVRead(STRING_HASH("GrassInstanceBufferLOD1"));
                b.DeclareBufferSRVRead(STRING_HASH("GrassInstanceBufferLOD2"));

                // Clear depth at frame begin (same as GBuffer pass, but now happens earlier)
                b.SetClearDepthStencil(kDepth, 1.f, 0);
            },
            [](RenderPassContext& ctx)
            {
                ASSERT(ctx.pImmediateContext, "Context is null.");
                ASSERT(ctx.pRegistry, "Registry is null.");

                IDeviceContext* pContext = ctx.pImmediateContext;

                IPipelineState* pLastPSO = nullptr;
                IShaderResourceBinding* pLastSRB = nullptr;
                IBuffer* pLastVB = nullptr;
                IBuffer* pLastIB = nullptr;

                // -------------------------
                // Direct packets
                // -------------------------
                for (const DrawPacket& pkt : ctx.DepthPrepassDrawPackets)
                {
                    ASSERT(pkt.PSO && pkt.SRB && pkt.VertexBuffer && pkt.IndexBuffer, "Invalid draw packet values.");

                    IPipelineState* pPSO = GetDepthPSO(pkt);
                    IShaderResourceBinding* pSRB = GetDepthSRB(pkt);

                    ASSERT(pPSO && pSRB, "Depth PSO/SRB is null (check GetDepthPSO/GetDepthSRB).");

                    if (pLastPSO != pPSO)
                    {
                        pLastPSO = pPSO;
                        pLastSRB = nullptr;
                        pContext->SetPipelineState(pLastPSO);
                    }

                    if (pLastSRB != pSRB)
                    {
                        pLastSRB = pSRB;
                        pContext->CommitShaderResources(pLastSRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);
                    }

                    if (pLastVB != pkt.VertexBuffer)
                    {
                        IBuffer* ppVertexBuffers[] = { pkt.VertexBuffer };
                        uint64 pOffsets[] = { 0 };

                        pContext->SetVertexBuffers(
                            0,
                            1,
                            ppVertexBuffers,
                            pOffsets,
                            RESOURCE_STATE_TRANSITION_MODE_VERIFY,
                            SET_VERTEX_BUFFERS_FLAG_RESET);

                        pLastVB = pkt.VertexBuffer;
                    }

                    if (pLastIB != pkt.IndexBuffer)
                    {
                        pContext->SetIndexBuffer(pkt.IndexBuffer, 0, RESOURCE_STATE_TRANSITION_MODE_VERIFY);
                        pLastIB = pkt.IndexBuffer;
                    }

                    DrawIndexedAttribs dia = pkt.DrawAttribs;

                    // DRAW_CONSTANTS update (StartInstanceLocation 유지)
                    {
                        MapHelper<hlsl::DrawConstants> map(
                            pContext,
                            ctx.pRegistry->GetBuffer(STRING_HASH("DRAW_CONSTANTS")),
                            MAP_WRITE,
                            MAP_FLAG_DISCARD);

                        hlsl::DrawConstants* dst = map;
                        dst->StartInstanceLocation = dia.FirstInstanceLocation;
                    }

                    pContext->DrawIndexed(dia);
                }

                // -------------------------
                // Indirect packets
                // -------------------------
                for (const DrawIndirectPacket& pkt : ctx.DepthPrepassIndirectDrawPackets)
                {
                    ASSERT(pkt.PSO && pkt.SRB && pkt.VertexBuffer && pkt.IndexBuffer, "Invalid draw packet values.");

                    IPipelineState* pPSO = GetDepthPSO(pkt);
                    IShaderResourceBinding* pSRB = GetDepthSRB(pkt);

                    ASSERT(pPSO && pSRB, "Depth PSO/SRB is null (check GetDepthPSO/GetDepthSRB).");

                    if (pLastPSO != pPSO)
                    {
                        pLastPSO = pPSO;
                        pLastSRB = nullptr;
                        pContext->SetPipelineState(pLastPSO);
                    }

                    if (pLastSRB != pSRB)
                    {
                        pLastSRB = pSRB;
                        pContext->CommitShaderResources(pLastSRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);
                    }

                    if (pLastVB != pkt.VertexBuffer)
                    {
                        IBuffer* ppVertexBuffers[] = { pkt.VertexBuffer };
                        uint64 pOffsets[] = { 0 };

                        pContext->SetVertexBuffers(
                            0,
                            1,
                            ppVertexBuffers,
                            pOffsets,
                            RESOURCE_STATE_TRANSITION_MODE_VERIFY,
                            SET_VERTEX_BUFFERS_FLAG_RESET);

                        pLastVB = pkt.VertexBuffer;
                    }

                    if (pLastIB != pkt.IndexBuffer)
                    {
                        pContext->SetIndexBuffer(pkt.IndexBuffer, 0, RESOURCE_STATE_TRANSITION_MODE_VERIFY);
                        pLastIB = pkt.IndexBuffer;
                    }

                    DrawIndexedIndirectAttribs dia = pkt.DrawAttribs;
                    pContext->DrawIndexedIndirect(dia);
                }
            });

        // ------------------------------------------------------------
        // Pass: GBuffer
        // ------------------------------------------------------------
        renderer.AddPass(
            "GBuffer",
            EPassExecutionDomain::RenderPass,
            [](RenderPassBuilder& b)
            {
                const uint64 kAlbedo = STRING_HASH("GBuffer0_Albedo");
                const uint64 kNormal = STRING_HASH("GBuffer1_Normal");
                const uint64 kMRAO = STRING_HASH("GBuffer2_MRAO");
                const uint64 kEmissive = STRING_HASH("GBuffer3_Emissive");
                const uint64 kVelocity = STRING_HASH("Velocity");
                const uint64 kDepth = STRING_HASH("GBufferDepth");

                b.DeclareTextureRTVWrite(kAlbedo);
                b.DeclareTextureRTVWrite(kNormal);
                b.DeclareTextureRTVWrite(kMRAO);
                b.DeclareTextureRTVWrite(kEmissive);
                b.DeclareTextureRTVWrite(kVelocity);
                b.DeclareTextureDSVWrite(kDepth);

                b.DeclareBufferIndirectArgsRead(STRING_HASH("IndirectArgsBuffer"));

                b.DeclareBufferSRVRead(STRING_HASH("GrassInstanceBufferLOD0"));
                b.DeclareBufferSRVRead(STRING_HASH("GrassInstanceBufferLOD1"));
                b.DeclareBufferSRVRead(STRING_HASH("GrassInstanceBufferLOD2"));

                b.SetClearColor(kAlbedo, 0.f, 0.f, 0.f, 0.f);
                b.SetClearColor(kNormal, 0.f, 0.f, 0.f, 0.f);
                b.SetClearColor(kMRAO, 0.f, 0.f, 0.f, 0.f);
                b.SetClearColor(kEmissive, 0.f, 0.f, 0.f, 0.f);
                b.SetClearColor(kVelocity, 0.f, 0.f, 0.f, 0.f);
            },
            [](RenderPassContext& ctx)
            {
                // (기존 네 코드 그대로)
                ASSERT(ctx.pImmediateContext, "Context is null.");
                ASSERT(ctx.pRegistry, "Registry is null.");

                IDeviceContext* pContext = ctx.pImmediateContext;

                IPipelineState* pLastPSO = nullptr;
                IShaderResourceBinding* pLastSRB = nullptr;
                IBuffer* pLastVB = nullptr;
                IBuffer* pLastIB = nullptr;

                for (const DrawPacket& pkt : ctx.MainDrawPackets)
                {
                    ASSERT(pkt.PSO && pkt.SRB && pkt.VertexBuffer && pkt.IndexBuffer, "Invalid draw packet values.");

                    if (pLastPSO != pkt.PSO)
                    {
                        pLastPSO = pkt.PSO;
                        pLastSRB = nullptr;
                        pContext->SetPipelineState(pLastPSO);
                    }

                    if (pLastSRB != pkt.SRB)
                    {
                        pLastSRB = pkt.SRB;
                        pContext->CommitShaderResources(pLastSRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);
                    }

                    if (pLastVB != pkt.VertexBuffer)
                    {
                        IBuffer* ppVertexBuffers[] = { pkt.VertexBuffer };
                        uint64 pOffsets[] = { 0 };

                        pContext->SetVertexBuffers(
                            0,
                            1,
                            ppVertexBuffers,
                            pOffsets,
                            RESOURCE_STATE_TRANSITION_MODE_VERIFY,
                            SET_VERTEX_BUFFERS_FLAG_RESET);

                        pLastVB = pkt.VertexBuffer;
                    }

                    if (pLastIB != pkt.IndexBuffer)
                    {
                        pContext->SetIndexBuffer(pkt.IndexBuffer, 0, RESOURCE_STATE_TRANSITION_MODE_VERIFY);
                        pLastIB = pkt.IndexBuffer;
                    }

                    DrawIndexedAttribs dia = pkt.DrawAttribs;

                    {
                        MapHelper<hlsl::DrawConstants> map(
                            pContext,
                            ctx.pRegistry->GetBuffer(STRING_HASH("DRAW_CONSTANTS")),
                            MAP_WRITE,
                            MAP_FLAG_DISCARD);

                        hlsl::DrawConstants* dst = map;
                        dst->StartInstanceLocation = dia.FirstInstanceLocation;
                    }

                    pContext->DrawIndexed(dia);
                }

                for (const DrawIndirectPacket& pkt : ctx.MainIndirectPackets)
                {
                    ASSERT(pkt.PSO && pkt.SRB && pkt.VertexBuffer && pkt.IndexBuffer, "Invalid draw packet values.");

                    if (pLastPSO != pkt.PSO)
                    {
                        pLastPSO = pkt.PSO;
                        pLastSRB = nullptr;
                        pContext->SetPipelineState(pLastPSO);
                    }

                    if (pLastSRB != pkt.SRB)
                    {
                        pLastSRB = pkt.SRB;
                        pContext->CommitShaderResources(pLastSRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);
                    }

                    if (pLastVB != pkt.VertexBuffer)
                    {
                        IBuffer* ppVertexBuffers[] = { pkt.VertexBuffer };
                        uint64 pOffsets[] = { 0 };

                        pContext->SetVertexBuffers(
                            0,
                            1,
                            ppVertexBuffers,
                            pOffsets,
                            RESOURCE_STATE_TRANSITION_MODE_VERIFY,
                            SET_VERTEX_BUFFERS_FLAG_RESET);

                        pLastVB = pkt.VertexBuffer;
                    }

                    if (pLastIB != pkt.IndexBuffer)
                    {
                        pContext->SetIndexBuffer(pkt.IndexBuffer, 0, RESOURCE_STATE_TRANSITION_MODE_VERIFY);
                        pLastIB = pkt.IndexBuffer;
                    }

                    DrawIndexedIndirectAttribs dia = pkt.DrawAttribs;
                    pContext->DrawIndexedIndirect(dia);
                }
            });

        // ------------------------------------------------------------
        // Pass: Lighting
        // ------------------------------------------------------------
        // (이하 네 코드 그대로)
        renderer.AddPass(
            "LightingScene",
            EPassExecutionDomain::RenderPass,
            [](RenderPassBuilder& b)
            {
                const uint64 kG0 = STRING_HASH("GBuffer0_Albedo");
                const uint64 kG1 = STRING_HASH("GBuffer1_Normal");
                const uint64 kG2 = STRING_HASH("GBuffer2_MRAO");
                const uint64 kG3 = STRING_HASH("GBuffer3_Emissive");
                const uint64 kGD = STRING_HASH("GBufferDepth");
                const uint64 kShadow = STRING_HASH("ShadowMap");
                const uint64 kLighting = STRING_HASH("LightingScene");

                b.DeclareTextureSRVRead(kG0);
                b.DeclareTextureSRVRead(kG1);
                b.DeclareTextureSRVRead(kG2);
                b.DeclareTextureSRVRead(kG3);
                b.DeclareTextureSRVRead(kGD);
                b.DeclareTextureSRVRead(kShadow);

                b.DeclareTextureRTVWrite(kLighting);
                b.SetClearColor(kLighting, 0.f, 0.f, 0.f, 1.f);
            },
            [this](RenderPassContext& ctx)
            {
                ASSERT(ctx.pImmediateContext, "Context is null.");
                ASSERT(ctx.pRegistry, "Registry is null.");
                ASSERT(m_pLightingPSO, "Lighting PSO is null. (onCreated must have initialized it)");
                ASSERT(m_pLightingSRB, "Lighting SRB is null. (onCreated must have initialized it)");

                auto bindTexture = [this](const char* name, ITextureView* srv)
                {
                    if (auto var = m_pLightingSRB->GetVariableByName(SHADER_TYPE_PIXEL, name))
                        var->Set(srv, SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
                };

                bindTexture("g_GBuffer0", ctx.pRegistry->GetTextureSRV(STRING_HASH("GBuffer0_Albedo")));
                bindTexture("g_GBuffer1", ctx.pRegistry->GetTextureSRV(STRING_HASH("GBuffer1_Normal")));
                bindTexture("g_GBuffer2", ctx.pRegistry->GetTextureSRV(STRING_HASH("GBuffer2_MRAO")));
                bindTexture("g_GBuffer3", ctx.pRegistry->GetTextureSRV(STRING_HASH("GBuffer3_Emissive")));
                bindTexture("g_GBufferDepth", ctx.pRegistry->GetTextureSRV(STRING_HASH("GBufferDepth")));
                bindTexture("g_ShadowMap", ctx.pRegistry->GetTextureSRV(STRING_HASH("ShadowMap")));

                IDeviceContext* pCtx = ctx.pImmediateContext;

                pCtx->SetPipelineState(m_pLightingPSO);
                pCtx->CommitShaderResources(m_pLightingSRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);

                DrawAttribs da = {};
                da.NumVertices = 3;
                da.Flags = DRAW_FLAG_VERIFY_ALL;
                pCtx->Draw(da);
            },
                [this, &renderer]()
            {
                // (네 onCreated 그대로)
                GraphicsPipelineStateCreateInfo psoCi = {};
                psoCi.PSODesc.Name = "Lighting PSO";
                psoCi.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

                GraphicsPipelineDesc& gp = psoCi.GraphicsPipeline;

                gp.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
                gp.RasterizerDesc.CullMode = CULL_MODE_BACK;
                gp.RasterizerDesc.FrontCounterClockwise = true;
                gp.DepthStencilDesc.DepthEnable = false;

                ShaderCreateInfo vsCI = {};
                vsCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
                vsCI.EntryPoint = "main";
                vsCI.Desc.Name = "Lighting VS";
                vsCI.Desc.ShaderType = SHADER_TYPE_VERTEX;
                vsCI.FilePath = m_LightingVS.c_str();

                ShaderCreateInfo psCI = {};
                psCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
                psCI.EntryPoint = "main";
                psCI.Desc.Name = "Lighting PS";
                psCI.Desc.ShaderType = SHADER_TYPE_PIXEL;
                psCI.FilePath = m_LightingPS.c_str();

                renderer.CreateShader(vsCI, &psoCi.pVS);
                renderer.CreateShader(psCI, &psoCi.pPS);

                psoCi.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

                ShaderResourceVariableDesc vars[] =
                {
                    { SHADER_TYPE_PIXEL, "g_GBuffer0",     SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
                    { SHADER_TYPE_PIXEL, "g_GBuffer1",     SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
                    { SHADER_TYPE_PIXEL, "g_GBuffer2",     SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
                    { SHADER_TYPE_PIXEL, "g_GBuffer3",     SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
                    { SHADER_TYPE_PIXEL, "g_GBufferDepth", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
                    { SHADER_TYPE_PIXEL, "g_ShadowMap",    SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
                };
                psoCi.PSODesc.ResourceLayout.Variables = vars;
                psoCi.PSODesc.ResourceLayout.NumVariables = _countof(vars);

                SamplerDesc linearClamp =
                {
                    FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR,
                    TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP
                };

                SamplerDesc shadowClamp = {};
                shadowClamp.MinFilter = FILTER_TYPE_COMPARISON_LINEAR;
                shadowClamp.MagFilter = FILTER_TYPE_COMPARISON_LINEAR;
                shadowClamp.MipFilter = FILTER_TYPE_COMPARISON_LINEAR;
                shadowClamp.AddressU = TEXTURE_ADDRESS_CLAMP;
                shadowClamp.AddressV = TEXTURE_ADDRESS_CLAMP;
                shadowClamp.AddressW = TEXTURE_ADDRESS_CLAMP;
                shadowClamp.ComparisonFunc = COMPARISON_FUNC_LESS_EQUAL;

                ImmutableSamplerDesc samplers[] =
                {
                    { SHADER_TYPE_PIXEL, "g_LinearClampSampler", linearClamp },
                    { SHADER_TYPE_PIXEL, "g_ShadowCmpSampler",   shadowClamp },
                };
                psoCi.PSODesc.ResourceLayout.ImmutableSamplers = samplers;
                psoCi.PSODesc.ResourceLayout.NumImmutableSamplers = _countof(samplers);

                m_pLightingPSO = renderer.AcquirePipelineState(STRING_HASH("LightingScene"), psoCi);
                ASSERT(m_pLightingPSO, "AcquirePipelineState(LightingScene) failed.");

                m_pLightingPSO->CreateShaderResourceBinding(&m_pLightingSRB, true);
                ASSERT(m_pLightingSRB, "Lighting SRB create failed.");
            });
    }
} // namespace shz
