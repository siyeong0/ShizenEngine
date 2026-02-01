#pragma once
#include <string>

#include "Engine/Core/Common/Public/RefCntAutoPtr.hpp"

#include "Engine/RHI/Interface/IRenderPass.h"
#include "Engine/RHI/Interface/IFramebuffer.h"
#include "Engine/RHI/Interface/IPipelineState.h"
#include "Engine/RHI/Interface/IShaderResourceBinding.h"
#include "Engine/RHI/Interface/IBuffer.h"

#include "Engine/Renderer/Public/RenderData.h"

#include "Engine/RenderPass/Public/RenderPassBase.h"
#include "Engine/RenderPass/Public/RenderPassContext.h"

namespace shz
{
	class GrassRenderPass final : public RenderPassBase
	{
	public:
		GrassRenderPass(RenderPassContext& ctx);
		~GrassRenderPass() override;

		const char* GetName() const override { return "Grass"; }

		void BeginFrame(RenderPassContext& ctx) override;
		void Execute(RenderPassContext& ctx) override;
		void EndFrame(RenderPassContext& ctx) override;

		void ReleaseSwapChainBuffers(RenderPassContext& ctx) override;
		void OnResize(RenderPassContext& ctx, uint32 width, uint32 height) override;

		IRenderPass* GetRHIRenderPass() override { return m_pRenderPass; };

		void SetGrassModel(RenderPassContext& ctx, const StaticMeshRenderData& mesh);
		void SetGrassDensityField(RenderPassContext& ctx, const TextureRenderData& tex);
	private:
		bool buildFramebufferForCurrentBackBuffer(RenderPassContext& ctx);

	private:
		static constexpr uint32 MAX_NUM_GRASS_INSTANCES = 1u << 24;
		static constexpr uint32 INTERACTION_FIELD_SIZE = 1025;
		static constexpr uint32 MAX_NUM_INTERACTION_STAMPS = 256;

		RefCntAutoPtr<IRenderPass> m_pRenderPass;
		RefCntAutoPtr<IFramebuffer> m_pFramebuffer;

		// Compute (2-pass: Generate + WriteArgs)
		RefCntAutoPtr<IPipelineState> m_pGenCSO;
		RefCntAutoPtr<IShaderResourceBinding> m_pGenCSRB;

		RefCntAutoPtr<IPipelineState> m_pArgsCSO;
		RefCntAutoPtr<IShaderResourceBinding> m_pArgsCSRB;

		// Graphics
		RefCntAutoPtr<IPipelineState> m_pGrassPSO;
		RefCntAutoPtr<IShaderResourceBinding> m_pGrassSRB;

		const StaticMeshRenderData* m_pGrassMesh;
		const TextureRenderData* m_pGrassDensityFieldTex;

		// Compute PSOs for interaction update
		RefCntAutoPtr<IPipelineState> m_pInteractionDecayCSO;
		RefCntAutoPtr<IShaderResourceBinding> m_pInteractionDecaySRB;

		RefCntAutoPtr<IPipelineState> m_pInteractionApplyCSO;
		RefCntAutoPtr<IShaderResourceBinding> m_pInteractionApplySRB;
	};
} // namespace shz
