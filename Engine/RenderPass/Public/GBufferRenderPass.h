#pragma once
#include <string>

#include "Engine/Core/Common/Public/RefCntAutoPtr.hpp"

#include "Engine/RHI/Interface/ITexture.h"
#include "Engine/RHI/Interface/ITextureView.h"
#include "Engine/RHI/Interface/IRenderPass.h"
#include "Engine/RHI/Interface/IFramebuffer.h"
#include "Engine/RHI/Interface/IPipelineState.h"
#include "Engine/RHI/Interface/IShaderResourceBinding.h"

#include "Engine/RenderPass/Public/RenderPassBase.h"
#include "Engine/RenderPass/Public/RenderPassContext.h"

namespace shz
{
	class GBufferRenderPass final : public RenderPassBase
	{
	public:
		const char* GetName() const override { return "GBuffer"; }

		bool Initialize(RenderPassContext& ctx) override;
		void Cleanup() override;

		void BeginFrame(RenderPassContext& ctx) override;
		void Execute(RenderPassContext& ctx, RenderScene& scene, const ViewFamily& viewFamily) override;
		void EndFrame(RenderPassContext& ctx) override;

		void ReleaseSwapChainBuffers(RenderPassContext& ctx) override;
		void OnResize(RenderPassContext& ctx, uint32 width, uint32 height) override;

		IRenderPass* GetRHIRenderPass() override { return m_pRenderPass; };
	public:
		ITextureView* GetGBufferSRV(uint32 index) const noexcept { return m_pGBufferSRV[index]; }
		ITextureView* GetDepthSRV() const noexcept { return m_pDepthSRV; }

	private:
		bool createTargets(RenderPassContext& ctx, uint32 width, uint32 height);
		bool createPassObjects(RenderPassContext& ctx);

	private:
		uint32 m_Width = 0;
		uint32 m_Height = 0;

		RefCntAutoPtr<ITexture> m_pGBufferTex[RenderPassContext::NUM_GBUFFERS];
		ITextureView* m_pGBufferRTV[RenderPassContext::NUM_GBUFFERS] = {};
		ITextureView* m_pGBufferSRV[RenderPassContext::NUM_GBUFFERS] = {};

		RefCntAutoPtr<ITexture> m_pDepthTex;
		ITextureView* m_pDepthDSV = nullptr;
		ITextureView* m_pDepthSRV = nullptr;

		RefCntAutoPtr<IRenderPass> m_pRenderPass;
		RefCntAutoPtr<IFramebuffer> m_pFramebuffer;

		std::string m_VS = "GBuffer.vsh";
		std::string m_PS = "GBuffer.psh";
	};
} // namespace shz
