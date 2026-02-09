#pragma once
#include <functional>
#include <string>
#include <vector>

#include "Primitives/BasicTypes.h"
#include "Engine/Renderer/Public/RenderGraphTypes.h"

namespace shz
{
	struct RenderPassContext;
	class IRenderPass;

	struct RenderPassBuilder final
	{
		std::vector<RenderPassResourceAccess> DeclaredAccesses;

		void DeclareTextureSRVRead(uint64 id)
		{
			RenderPassResourceAccess a = {};
			a.ResourceId = id;
			a.Kind = RENDER_RESOURCE_KIND_TEXTURE;
			a.Access = RENDER_ACCESS_READ;
			a.Usage = RENDER_USAGE_SRV;
			a.TextureViewType = TEXTURE_VIEW_SHADER_RESOURCE;
			DeclaredAccesses.push_back(a);
		}

		void DeclareTextureRTVWrite(uint64 id)
		{
			RenderPassResourceAccess a = {};
			a.ResourceId = id;
			a.Kind = RENDER_RESOURCE_KIND_TEXTURE;
			a.Access = RENDER_ACCESS_WRITE;
			a.Usage = RENDER_USAGE_RTV;
			a.TextureViewType = TEXTURE_VIEW_RENDER_TARGET;
			DeclaredAccesses.push_back(a);
		}

		void DeclareTextureRTVReadWrite(uint64 id)
		{
			RenderPassResourceAccess a = {};
			a.ResourceId = id;
			a.Kind = RENDER_RESOURCE_KIND_TEXTURE;
			a.Access = RENDER_ACCESS_READWRITE;
			a.Usage = RENDER_USAGE_RTV;
			a.TextureViewType = TEXTURE_VIEW_RENDER_TARGET;
			DeclaredAccesses.push_back(a);
		}

		void DeclareTextureDSVRead(uint64 id)
		{
			RenderPassResourceAccess a = {};
			a.ResourceId = id;
			a.Kind = RENDER_RESOURCE_KIND_TEXTURE;
			a.Access = RENDER_ACCESS_READ;
			a.Usage = RENDER_USAGE_DSV_READ;
			a.TextureViewType = TEXTURE_VIEW_DEPTH_STENCIL;
			DeclaredAccesses.push_back(a);
		}

		void DeclareTextureDSVWrite(uint64 id)
		{
			RenderPassResourceAccess a = {};
			a.ResourceId = id;
			a.Kind = RENDER_RESOURCE_KIND_TEXTURE;
			a.Access = RENDER_ACCESS_WRITE;
			a.Usage = RENDER_USAGE_DSV_WRITE;
			a.TextureViewType = TEXTURE_VIEW_DEPTH_STENCIL;
			DeclaredAccesses.push_back(a);
		}

		void DeclareTextureDSVReadWrite(uint64 id)
		{
			RenderPassResourceAccess a = {};
			a.ResourceId = id;
			a.Kind = RENDER_RESOURCE_KIND_TEXTURE;
			a.Access = RENDER_ACCESS_READWRITE;
			a.Usage = RENDER_USAGE_DSV_WRITE;
			a.TextureViewType = TEXTURE_VIEW_DEPTH_STENCIL;
			DeclaredAccesses.push_back(a);
		}

		void DeclareTextureUAV(uint64 id, ERenderAccess access = RENDER_ACCESS_READWRITE)
		{
			RenderPassResourceAccess a = {};
			a.ResourceId = id;
			a.Kind = RENDER_RESOURCE_KIND_TEXTURE;
			a.Access = access;
			a.Usage = RENDER_USAGE_UAV;
			a.TextureViewType = TEXTURE_VIEW_UNORDERED_ACCESS;
			DeclaredAccesses.push_back(a);
		}

		void DeclareBufferCBVRead(uint64 id)
		{
			RenderPassResourceAccess a = {};
			a.ResourceId = id;
			a.Kind = RENDER_RESOURCE_KIND_BUFFER;
			a.Access = RENDER_ACCESS_READ;
			a.Usage = RENDER_USAGE_CBV;
			DeclaredAccesses.push_back(a);
		}

		void DeclareBufferSRVRead(uint64 id)
		{
			RenderPassResourceAccess a = {};
			a.ResourceId = id;
			a.Kind = RENDER_RESOURCE_KIND_BUFFER;
			a.Access = RENDER_ACCESS_READ;
			a.Usage = RENDER_USAGE_SRV;
			DeclaredAccesses.push_back(a);
		}

		void DeclareBufferUAV(uint64 id, ERenderAccess access = RENDER_ACCESS_READWRITE)
		{
			RenderPassResourceAccess a = {};
			a.ResourceId = id;
			a.Kind = RENDER_RESOURCE_KIND_BUFFER;
			a.Access = access;
			a.Usage = RENDER_USAGE_UAV;
			DeclaredAccesses.push_back(a);
		}

		void DeclareBufferIndirectArgsRead(uint64 id)
		{
			RenderPassResourceAccess a = {};
			a.ResourceId = id;
			a.Kind = RENDER_RESOURCE_KIND_BUFFER;
			a.Access = RENDER_ACCESS_READ;
			a.Usage = RENDER_USAGE_INDIRECT_ARGUMENT;
			DeclaredAccesses.push_back(a);
		}

		void DeclareBufferIndirectArgsWrite(uint64 id)
		{
			RenderPassResourceAccess a = {};
			a.ResourceId = id;
			a.Kind = RENDER_RESOURCE_KIND_BUFFER;
			a.Access = RENDER_ACCESS_WRITE;
			a.Usage = RENDER_USAGE_INDIRECT_ARGUMENT;
			DeclaredAccesses.push_back(a);
		}

		void DeclareExternal(uint64 id, ERenderAccess access, ERenderUsage usage)
		{
			RenderPassResourceAccess a = {};
			a.ResourceId = id;
			a.Kind = RENDER_RESOURCE_KIND_EXTERNAL;
			a.Access = access;
			a.Usage = usage;
			DeclaredAccesses.push_back(a);
		}

		void DeclareSwapChainRTVWrite()
		{
			DeclareExternal(STRING_HASH("SwapChain.BackBuffer"), RENDER_ACCESS_WRITE, RENDER_USAGE_RTV);
		}

		std::unordered_map<uint64, OptimizedClearValue> ClearValues;

		void SetClearColor(uint64 id, float r, float g, float b, float a)
		{
			OptimizedClearValue cv = {};
			cv.Color[0] = r;
			cv.Color[1] = g;
			cv.Color[2] = b;
			cv.Color[3] = a;
			ClearValues[id] = cv;
		}

		void SetClearDepthStencil(uint64 id, float depth, uint8 stencil = 0)
		{
			OptimizedClearValue cv = {};
			cv.DepthStencil.Depth = depth;
			cv.DepthStencil.Stencil = stencil;
			ClearValues[id] = cv;
		}

		void SetDefaultClearBlack(uint64 id)
		{
			SetClearColor(id, 0.f, 0.f, 0.f, 0.f); 
		}

		void SetDefaultClearDepth1(uint64 id) 
		{ 
			SetClearDepthStencil(id, 1.f, 0);
		}
	};
} // namespace shz