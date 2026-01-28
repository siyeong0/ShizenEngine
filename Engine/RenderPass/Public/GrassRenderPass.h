#pragma once
#include <string>

#include "Engine/Core/Common/Public/RefCntAutoPtr.hpp"

#include "Engine/RHI/Interface/IRenderPass.h"
#include "Engine/RHI/Interface/IFramebuffer.h"
#include "Engine/RHI/Interface/IPipelineState.h"
#include "Engine/RHI/Interface/IShaderResourceBinding.h"
#include "Engine/RHI/Interface/IBuffer.h"

#include "Engine/Renderer/Public/RenderData.h"

#include "Engine/RenderPass/Public/RenderPassBase.h"
#include "Engine/RenderPass/Public/RenderPassContext.h"

namespace shz
{
    class GrassRenderPass final : public RenderPassBase
    {
    public:
        GrassRenderPass(RenderPassContext& ctx);
        ~GrassRenderPass() override;

        const char* GetName() const override { return "Grass"; }

        void BeginFrame(RenderPassContext& ctx) override;
        void Execute(RenderPassContext& ctx) override;
        void EndFrame(RenderPassContext& ctx) override;

        void ReleaseSwapChainBuffers(RenderPassContext& ctx) override;
        void OnResize(RenderPassContext& ctx, uint32 width, uint32 height) override;

        IRenderPass* GetRHIRenderPass() override { return m_pRenderPass; };

        void SetGrassModel(RenderPassContext& ctx, const StaticMeshRenderData& mesh);
        void SetGrassDensityField(RenderPassContext& ctx, const TextureRenderData& tex);
    private:
        bool buildFramebufferForCurrentBackBuffer(RenderPassContext& ctx);

    private:
        RefCntAutoPtr<IRenderPass>   m_pRenderPass;
        RefCntAutoPtr<IFramebuffer>  m_pFramebuffer;

        // Compute (2-pass: Generate + WriteArgs)
        RefCntAutoPtr<IPipelineState>         m_pGenCSO;
        RefCntAutoPtr<IShaderResourceBinding> m_pGenCSRB;

        RefCntAutoPtr<IPipelineState>         m_pArgsCSO;
        RefCntAutoPtr<IShaderResourceBinding> m_pArgsCSRB;

        // Graphics
        RefCntAutoPtr<IPipelineState>         m_pGrassPSO;
        RefCntAutoPtr<IShaderResourceBinding> m_pGrassSRB;

        // Buffers
        RefCntAutoPtr<IBuffer> m_pGrassInstanceBuffer;    // SRV/UAV
        RefCntAutoPtr<IBuffer> m_pIndirectArgsBuffer;     // INDIRECT DRAW ARGS
        RefCntAutoPtr<IBuffer> m_pCounterBuffer;          // UAV (uint)

        // NEW: CBs (match updated HLSL)
        RefCntAutoPtr<IBuffer> m_pGrassGenConstantsCB;    // GRASS_GEN_CONSTANTS (CS)
        RefCntAutoPtr<IBuffer> m_pGrassRenderConstantsCB; // GRASS_RENDER_CONSTANTS (VS/PS)

        uint32 m_MaxInstances = 1u << 24;

        const StaticMeshRenderData* m_pGrassMesh;
        const TextureRenderData* m_pGrassDensityFieldTex;
    };
} // namespace shz
