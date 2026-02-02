#pragma once
#include <string>

#include "Engine/Core/Common/Public/RefCntAutoPtr.hpp"

#include "Engine/RHI/Interface/IPipelineState.h"
#include "Engine/RHI/Interface/IShaderResourceBinding.h"

#include "Engine/RenderPass/Public/RenderPassBase.h"
#include "Engine/RenderPass/Public/RenderPassContext.h"

namespace shz
{
	class GrassBuildInstancesPass final : public RenderPassBase
	{
	public:
		GrassBuildInstancesPass();
		~GrassBuildInstancesPass() override;

		void Initialize(RenderPassContext& ctx) override;

		const char* GetName() const override { return "GrassBuild"; }

		void BeginFrame(RenderPassContext& ctx) override;
		void Execute(RenderPassContext& ctx) override;
		void EndFrame(RenderPassContext& ctx) override;

		void ReleaseSwapChainBuffers(RenderPassContext& ctx) override;
		void OnResize(RenderPassContext& ctx, uint32 width, uint32 height) override;

		IRenderPass* GetRHIRenderPass() override { return nullptr; }

	private:
		RefCntAutoPtr<IPipelineState> m_pGenCSO;
		RefCntAutoPtr<IShaderResourceBinding> m_pGenCSRB;

		RefCntAutoPtr<IPipelineState> m_pArgsCSO;
		RefCntAutoPtr<IShaderResourceBinding> m_pArgsCSRB;
	};
} // namespace shz
