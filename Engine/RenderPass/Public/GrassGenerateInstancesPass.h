#pragma once
#include <string>

#include "Engine/Core/Common/Public/RefCntAutoPtr.hpp"

#include "Engine/RHI/Interface/IPipelineState.h"
#include "Engine/RHI/Interface/IShaderResourceBinding.h"

#include "Engine/RenderPass/Public/RenderPassBase.h"
#include "Engine/Renderer/Public/RenderPassContext.h"

namespace shz
{
	class GrassGenerateInstancesPass final : public RenderPassBase
	{
	public:
		GrassGenerateInstancesPass();
		~GrassGenerateInstancesPass() override;

		void Initialize(RenderPassContext& ctx) override;

		const char* GetName() const override { return "GrassGenerateInstances"; }

		void BeginFrame(RenderPassContext& ctx) override;
		void Execute(RenderPassContext& ctx) override;
		void EndFrame(RenderPassContext& ctx) override;

		void ReleaseSwapChainBuffers(RenderPassContext& ctx) override;
		void OnResize(RenderPassContext& ctx, uint32 width, uint32 height) override;

		IRenderPass* GetRHIRenderPass() override { return nullptr; }

	private:
		RefCntAutoPtr<IPipelineState>         m_pCSO;
		RefCntAutoPtr<IShaderResourceBinding> m_pCSRB;
	};
} // namespace shz
