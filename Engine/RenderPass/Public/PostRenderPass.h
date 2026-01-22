#pragma once
#include <string>

#include "Engine/Core/Common/Public/RefCntAutoPtr.hpp"

#include "Engine/RHI/Interface/IRenderPass.h"
#include "Engine/RHI/Interface/IFramebuffer.h"
#include "Engine/RHI/Interface/IPipelineState.h"
#include "Engine/RHI/Interface/IShaderResourceBinding.h"

#include "Engine/RenderPass/Public/RenderPassBase.h"
#include "Engine/RenderPass/Public/RenderPassContext.h"

namespace shz
{
	class PostRenderPass final : public RenderPassBase
	{
	public:
		const char* GetName() const override { return "Post"; }

		bool Initialize(RenderPassContext& ctx) override;
		void Cleanup() override;

		void BeginFrame(RenderPassContext& ctx) override;
		void Execute(RenderPassContext& ctx) override;
		void EndFrame(RenderPassContext& ctx) override;

		void ReleaseSwapChainBuffers(RenderPassContext& ctx) override;
		void OnResize(RenderPassContext& ctx, uint32 width, uint32 height) override;

		IRenderPass* GetRHIRenderPass() override { return m_pRenderPass; };
	private:
		bool createRenderPass(RenderPassContext& ctx);
		bool buildFramebufferForCurrentBackBuffer(RenderPassContext& ctx);
		bool createPSO(RenderPassContext& ctx);
		void bindInputs(RenderPassContext& ctx);

	private:
		RefCntAutoPtr<IRenderPass> m_pRenderPass;
		RefCntAutoPtr<IFramebuffer> m_pFramebufferCurrentBB;

		RefCntAutoPtr<IPipelineState> m_pPSO;
		RefCntAutoPtr<IShaderResourceBinding> m_pSRB;

		std::string m_VS = "PostCopy.vsh";
		std::string m_PS = "PostCopy.psh";
	};
} // namespace shz
