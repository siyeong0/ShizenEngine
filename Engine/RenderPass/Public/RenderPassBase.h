#pragma once
#include <vector>
#include <span>

#include "Primitives/BasicTypes.h"
#include "Engine/Renderer/Public/RenderGraphTypes.h"

namespace shz
{
	struct RenderPassContext;
	class RenderScene;
	struct ViewFamily;
	class IRenderPass;

	class RenderPassBase
	{
	public:
		RenderPassBase() = default;
		RenderPassBase(const RenderPassBase&) = delete;
		RenderPassBase& operator=(const RenderPassBase&) = delete;
		virtual ~RenderPassBase() {}

		virtual void Initialize(RenderPassContext& ctx) = 0;

		virtual const char* GetName() const = 0;

		virtual void BeginFrame(RenderPassContext& ctx) = 0;
		virtual void Execute(RenderPassContext& ctx) = 0;
		virtual void EndFrame(RenderPassContext& ctx) = 0;

		virtual void ReleaseSwapChainBuffers(RenderPassContext& ctx) = 0;
		virtual void OnResize(RenderPassContext& ctx, uint32 width, uint32 height) = 0;

		virtual IRenderPass* GetRHIRenderPass() = 0;

		uint64 GetDrawCallCount() const { return m_DrawCallCount; }

		// -----------------------------------------------------------------
		// RenderGraph: declare resources this pass reads/writes
		// - Renderer가 이걸 보고 의존성/Transition/order를 자동화
		// -----------------------------------------------------------------
		std::span<const RenderPassResourceAccess> GetDeclaredResourceAccesses() const
		{
			return m_DeclaredAccesses;
		}

	protected:
		void DeclareTextureSRVRead(uint64 id)
		{
			RenderPassResourceAccess a = {};
			a.ResourceId = id;
			a.Kind = RENDER_RESOURCE_KIND_TEXTURE;
			a.Access = RENDER_ACCESS_READ;
			a.Usage = RENDER_USAGE_SRV;
			a.TextureViewType = TEXTURE_VIEW_SHADER_RESOURCE;
			m_DeclaredAccesses.push_back(a);
		}

		void DeclareTextureRTVWrite(uint64 id)
		{
			RenderPassResourceAccess a = {};
			a.ResourceId = id;
			a.Kind = RENDER_RESOURCE_KIND_TEXTURE;
			a.Access = RENDER_ACCESS_WRITE;
			a.Usage = RENDER_USAGE_RTV;
			a.TextureViewType = TEXTURE_VIEW_RENDER_TARGET;
			m_DeclaredAccesses.push_back(a);
		}

		void DeclareTextureRTVReadWrite(uint64 id)
		{
			RenderPassResourceAccess a = {};
			a.ResourceId = id;
			a.Kind = RENDER_RESOURCE_KIND_TEXTURE;
			a.Access = RENDER_ACCESS_READWRITE;
			a.Usage = RENDER_USAGE_RTV;
			a.TextureViewType = TEXTURE_VIEW_RENDER_TARGET;
			m_DeclaredAccesses.push_back(a);
		}

		void DeclareTextureDSVRead(uint64 id)
		{
			RenderPassResourceAccess a = {};
			a.ResourceId = id;
			a.Kind = RENDER_RESOURCE_KIND_TEXTURE;
			a.Access = RENDER_ACCESS_READ;
			a.Usage = RENDER_USAGE_DSV_READ;
			a.TextureViewType = TEXTURE_VIEW_DEPTH_STENCIL;
			m_DeclaredAccesses.push_back(a);
		}

		void DeclareTextureDSVWrite(uint64 id)
		{
			RenderPassResourceAccess a = {};
			a.ResourceId = id;
			a.Kind = RENDER_RESOURCE_KIND_TEXTURE;
			a.Access = RENDER_ACCESS_WRITE;
			a.Usage = RENDER_USAGE_DSV_WRITE;
			a.TextureViewType = TEXTURE_VIEW_DEPTH_STENCIL;
			m_DeclaredAccesses.push_back(a);
		}

		void DeclareTextureDSVReadWrite(uint64 id)
		{
			RenderPassResourceAccess a = {};
			a.ResourceId = id;
			a.Kind = RENDER_RESOURCE_KIND_TEXTURE;
			a.Access = RENDER_ACCESS_READWRITE;
			a.Usage = RENDER_USAGE_DSV_WRITE;
			a.TextureViewType = TEXTURE_VIEW_DEPTH_STENCIL;
			m_DeclaredAccesses.push_back(a);
		}

		void DeclareTextureUAV(uint64 id, ERenderAccess access = RENDER_ACCESS_READWRITE)
		{
			RenderPassResourceAccess a = {};
			a.ResourceId = id;
			a.Kind = RENDER_RESOURCE_KIND_TEXTURE;
			a.Access = access;
			a.Usage = RENDER_USAGE_UAV;
			a.TextureViewType = TEXTURE_VIEW_UNORDERED_ACCESS;
			m_DeclaredAccesses.push_back(a);
		}

		void DeclareBufferCBVRead(uint64 id)
		{
			RenderPassResourceAccess a = {};
			a.ResourceId = id;
			a.Kind = RENDER_RESOURCE_KIND_BUFFER;
			a.Access = RENDER_ACCESS_READ;
			a.Usage = RENDER_USAGE_CBV;
			m_DeclaredAccesses.push_back(a);
		}

		void DeclareBufferSRVRead(uint64 id)
		{
			RenderPassResourceAccess a = {};
			a.ResourceId = id;
			a.Kind = RENDER_RESOURCE_KIND_BUFFER;
			a.Access = RENDER_ACCESS_READ;
			a.Usage = RENDER_USAGE_SRV;
			m_DeclaredAccesses.push_back(a);
		}

		void DeclareBufferUAV(uint64 id, ERenderAccess access = RENDER_ACCESS_READWRITE)
		{
			RenderPassResourceAccess a = {};
			a.ResourceId = id;
			a.Kind = RENDER_RESOURCE_KIND_BUFFER;
			a.Access = access;
			a.Usage = RENDER_USAGE_UAV;
			m_DeclaredAccesses.push_back(a);
		}

		void DeclareBufferIndirectArgsRead(uint64 id)
		{
			RenderPassResourceAccess a = {};
			a.ResourceId = id;
			a.Kind = RENDER_RESOURCE_KIND_BUFFER;
			a.Access = RENDER_ACCESS_READ;
			a.Usage = RENDER_USAGE_INDIRECT_ARGUMENT;
			m_DeclaredAccesses.push_back(a);
		}

		void DeclareExternal(uint64 id, ERenderAccess access, ERenderUsage usage)
		{
			RenderPassResourceAccess a = {};
			a.ResourceId = id;
			a.Kind = RENDER_RESOURCE_KIND_EXTERNAL;
			a.Access = access;
			a.Usage = usage;
			m_DeclaredAccesses.push_back(a);
		}

		void DeclareSwapChainRTVWrite()
		{
			DeclareExternal(STRING_HASH("SwapChain.BackBuffer"), RENDER_ACCESS_WRITE, RENDER_USAGE_RTV);
		}

	protected:
		uint64 m_DrawCallCount = 0;

	private:
		std::vector<RenderPassResourceAccess> m_DeclaredAccesses = {};
	};
} // namespace shz
