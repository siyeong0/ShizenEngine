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
	class ShadowRenderPass final : public RenderPassBase
	{
	public:
		ShadowRenderPass(RenderPassContext& ctx);
		~ShadowRenderPass() override;

		const char* GetName() const override { return "Shadow"; }

		void BeginFrame(RenderPassContext& ctx) override;
		void Execute(RenderPassContext& ctx) override;
		void EndFrame(RenderPassContext& ctx) override;

		void ReleaseSwapChainBuffers(RenderPassContext& ctx) override;
		void OnResize(RenderPassContext& ctx, uint32 width, uint32 height) override;

		IRenderPass* GetRHIRenderPass() override { return m_pRenderPass; };
	public:
		IPipelineState* GetShadowMaskedPSO() const noexcept { return m_pShadowMaskedPSO.RawPtr(); }
		IPipelineState* GetShadowPSO() const noexcept { return m_pShadowPSO.RawPtr(); }
		IShaderResourceBinding* GetOpaqueShadowSRB() const noexcept { return m_pSRB.RawPtr(); }

	private:
		RefCntAutoPtr<IRenderPass> m_pRenderPass;
		RefCntAutoPtr<IFramebuffer> m_pFramebuffer;

		RefCntAutoPtr<IPipelineState> m_pShadowPSO;
		RefCntAutoPtr<IPipelineState> m_pShadowMaskedPSO;
		RefCntAutoPtr<IShaderResourceBinding> m_pSRB;

		std::string m_VS = "Shadow.vsh";
		std::string m_PS = "Shadow.psh"; 

		std::string m_MaskedVS = "ShadowMasked.vsh";
		std::string m_MaskedPS = "ShadowMasked.psh";
	};
} // namespace shz
