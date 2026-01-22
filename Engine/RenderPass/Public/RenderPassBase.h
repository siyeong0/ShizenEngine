#pragma once
#include "Primitives/BasicTypes.h"

namespace shz
{
	struct RenderPassContext;
	class RenderScene;
	struct ViewFamily;

	class RenderPassBase
	{
	public:
		RenderPassBase() = default;
		RenderPassBase(const RenderPassBase&) = delete;
		RenderPassBase& operator=(const RenderPassBase&) = delete;
		virtual ~RenderPassBase() = default;

		virtual const char* GetName() const = 0;

		virtual bool Initialize(RenderPassContext& ctx) = 0;
		virtual void Cleanup() = 0;

		virtual void BeginFrame(RenderPassContext& ctx) = 0;
		virtual void Execute(RenderPassContext& ctx, RenderScene& scene, const ViewFamily& viewFamily) = 0;
		virtual void EndFrame(RenderPassContext& ctx) = 0;

		virtual void ReleaseSwapChainBuffers(RenderPassContext& ctx) = 0;
		virtual void OnResize(RenderPassContext& ctx, uint32 width, uint32 height) = 0;

		virtual IRenderPass* GetRHIRenderPass() = 0;
	};
} // namespace shz
