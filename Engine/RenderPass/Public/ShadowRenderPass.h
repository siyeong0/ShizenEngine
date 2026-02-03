#pragma once
#include "Engine/Core/Common/Public/RefCntAutoPtr.hpp"

#include "Engine/RHI/Interface/ITexture.h"
#include "Engine/RHI/Interface/ITextureView.h"
#include "Engine/RHI/Interface/IRenderPass.h"
#include "Engine/RHI/Interface/IFramebuffer.h"
#include "Engine/RHI/Interface/IPipelineState.h"
#include "Engine/RHI/Interface/IShaderResourceBinding.h"

#include "Engine/RenderPass/Public/RenderPassBase.h"
#include "Engine/Renderer/Public/RenderPassContext.h"

namespace shz
{
	class ShadowRenderPass final : public RenderPassBase
	{
	public:
		ShadowRenderPass();
		~ShadowRenderPass() override;

		void Initialize(RenderPassContext& ctx) override;

		const char* GetName() const override { return "Shadow"; }

		void BeginFrame(RenderPassContext& ctx) override;
		void Execute(RenderPassContext& ctx) override;
		void EndFrame(RenderPassContext& ctx) override;

		void ReleaseSwapChainBuffers(RenderPassContext& ctx) override;
		void OnResize(RenderPassContext& ctx, uint32 width, uint32 height) override;

		IRenderPass* GetRHIRenderPass() override { return m_pRenderPass; };

	private:
		RefCntAutoPtr<IRenderPass> m_pRenderPass;
		RefCntAutoPtr<IFramebuffer> m_pFramebuffer;

		std::string m_ShadowVS = "Shadow.vsh";
		std::string m_ShadowPS = "Shadow.psh";
		std::string m_ShadowMaskedVS = "ShadowMasked.vsh";
		std::string m_ShadowMaskedPS = "ShadowMasked.psh";

	public:
		RefCntAutoPtr<IPipelineState> m_pShadowPSO;
		RefCntAutoPtr<IPipelineState> m_pShadowMaskedPSO;
		RefCntAutoPtr<IShaderResourceBinding> m_pShadowSRB;
	};
} // namespace shz
