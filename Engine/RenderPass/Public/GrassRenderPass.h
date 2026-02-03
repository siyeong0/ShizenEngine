#pragma once
#include <string>

#include "Engine/Core/Common/Public/RefCntAutoPtr.hpp"

#include "Engine/RHI/Interface/IRenderPass.h"
#include "Engine/RHI/Interface/IFramebuffer.h"
#include "Engine/RHI/Interface/IPipelineState.h"
#include "Engine/RHI/Interface/IShaderResourceBinding.h"

#include "Engine/Renderer/Public/StaticMeshRenderData.h"

#include "Engine/RenderPass/Public/RenderPassBase.h"
#include "Engine/Renderer/Public/RenderPassContext.h"

namespace shz
{
	class GrassRenderPass final : public RenderPassBase
	{
	public:
		GrassRenderPass();
		~GrassRenderPass() override;

		void Initialize(RenderPassContext& ctx) override;

		const char* GetName() const override { return "Forward"; }

		void BeginFrame(RenderPassContext& ctx) override;
		void Execute(RenderPassContext& ctx) override;
		void EndFrame(RenderPassContext& ctx) override;

		void ReleaseSwapChainBuffers(RenderPassContext& ctx) override;
		void OnResize(RenderPassContext& ctx, uint32 width, uint32 height) override;

		IRenderPass* GetRHIRenderPass() override { return m_pRenderPass; };

	private:
		bool buildFramebuffer(RenderPassContext& ctx);

	private:
		RefCntAutoPtr<IRenderPass> m_pRenderPass;
		RefCntAutoPtr<IFramebuffer> m_pFramebuffer;

		RefCntAutoPtr<IPipelineState> m_pGrassPSO;
		RefCntAutoPtr<IShaderResourceBinding> m_pGrassSRB;

		const StaticMeshRenderData* m_pGrassMesh = nullptr;
	};
} // namespace shz
