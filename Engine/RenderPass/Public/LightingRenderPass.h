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
	class LightingRenderPass final : public RenderPassBase
	{
	public:
		const char* GetName() const override { return "Lighting"; }

		bool Initialize(RenderPassContext& ctx) override;
		void Cleanup() override;

		void BeginFrame(RenderPassContext& ctx) override;
		void Execute(RenderPassContext& ctx, RenderScene& scene, const ViewFamily& viewFamily) override;
		void EndFrame(RenderPassContext& ctx) override;

		void ReleaseSwapChainBuffers(RenderPassContext& ctx) override;
		void OnResize(RenderPassContext& ctx, uint32 width, uint32 height) override;

		IRenderPass* GetRHIRenderPass() override { return m_pRenderPass; };
	public:
		ITextureView* GetLightingSRV() const noexcept { return m_pLightingSRV; }

	private:
		bool createTargets(RenderPassContext& ctx, uint32 width, uint32 height);
		bool createPassObjects(RenderPassContext& ctx);
		bool createPSO(RenderPassContext& ctx);
		void bindInputs(RenderPassContext& ctx);

	private:
		uint32 m_Width = 0;
		uint32 m_Height = 0;

		RefCntAutoPtr<ITexture> m_pLightingTex;
		ITextureView* m_pLightingRTV = nullptr;
		ITextureView* m_pLightingSRV = nullptr;

		RefCntAutoPtr<IRenderPass> m_pRenderPass;
		RefCntAutoPtr<IFramebuffer> m_pFramebuffer;

		RefCntAutoPtr<IPipelineState> m_pPSO;
		RefCntAutoPtr<IShaderResourceBinding> m_pSRB;

		std::string m_VS = "Lighting.vsh";
		std::string m_PS = "Lighting.psh";
	};
} // namespace shz
