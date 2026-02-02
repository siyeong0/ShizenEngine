#pragma once
#include <string>
#include <vector>

#include "Engine/Core/Common/Public/RefCntAutoPtr.hpp"

#include "Engine/RHI/Interface/IPipelineState.h"
#include "Engine/RHI/Interface/IShaderResourceBinding.h"

#include "Engine/RenderPass/Public/RenderPassBase.h"
#include "Engine/RenderPass/Public/RenderPassContext.h"

namespace shz
{
	class GrassInteractionPass final : public RenderPassBase
	{
	public:
		GrassInteractionPass();
		~GrassInteractionPass() override;

		void Initialize(RenderPassContext& ctx) override;

		const char* GetName() const override { return "GrassInteraction"; }

		void BeginFrame(RenderPassContext& ctx) override;
		void Execute(RenderPassContext& ctx) override;
		void EndFrame(RenderPassContext& ctx) override;

		void ReleaseSwapChainBuffers(RenderPassContext& ctx) override;
		void OnResize(RenderPassContext& ctx, uint32 width, uint32 height) override;

		IRenderPass* GetRHIRenderPass() override { return nullptr; }

	private:
		static constexpr uint32 INTERACTION_FIELD_SIZE = 1025;
		static constexpr uint32 MAX_NUM_INTERACTION_STAMPS = 256;

		RefCntAutoPtr<IPipelineState> m_pDecayCSO;
		RefCntAutoPtr<IShaderResourceBinding> m_pDecaySRB;

		RefCntAutoPtr<IPipelineState> m_pApplyCSO;
		RefCntAutoPtr<IShaderResourceBinding> m_pApplySRB;
	};
} // namespace shz
