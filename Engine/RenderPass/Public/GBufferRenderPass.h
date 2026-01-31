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
		GBufferRenderPass(RenderPassContext& ctx);
		~GBufferRenderPass() override;

		const char* GetName() const override { return "GBuffer"; }

		void BeginFrame(RenderPassContext& ctx) override;
		void Execute(RenderPassContext& ctx) override;
		void EndFrame(RenderPassContext& ctx) override;

		void ReleaseSwapChainBuffers(RenderPassContext& ctx) override;
		void OnResize(RenderPassContext& ctx, uint32 width, uint32 height) override;

		IRenderPass* GetRHIRenderPass() override { return m_pRenderPass; };

	private:
		bool createPassObjects(RenderPassContext& ctx);

	private:
		RefCntAutoPtr<IRenderPass> m_pRenderPass;
		RefCntAutoPtr<IFramebuffer> m_pFramebuffer;

		std::string m_VS = "GBuffer.vsh";
		std::string m_PS = "GBuffer.psh";
	};
} // namespace shz
