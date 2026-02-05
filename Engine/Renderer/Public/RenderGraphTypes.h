#pragma once
#include <cstdint>

#include "Primitives/BasicTypes.h"
#include "Engine/RHI/Interface/GraphicsTypes.h"

namespace shz
{
	// ---------------------------------------------------------------------
	// Resource kind (registry lookup type)
	// ---------------------------------------------------------------------
	enum ERenderResourceKind : uint8
	{
		RENDER_RESOURCE_KIND_TEXTURE = 0,
		RENDER_RESOURCE_KIND_BUFFER = 1,
		RENDER_RESOURCE_KIND_EXTERNAL = 2, // swapchain backbuffer 등 registry 밖
	};

	// ---------------------------------------------------------------------
	// How the pass uses the resource
	// ---------------------------------------------------------------------
	enum ERenderAccess : uint8
	{
		RENDER_ACCESS_READ = 0,
		RENDER_ACCESS_WRITE = 1,
		RENDER_ACCESS_READWRITE = 2,
	};

	enum class EPassExecutionDomain : uint8
	{
		RenderPass,   // inside render pass (RT/DS attachments)
		OutsideRenderPass, // no render pass begin/end
	};

	// ---------------------------------------------------------------------
	// What view/state the access implies (minimal set)
	// - Renderer가 이걸 보고 RESOURCE_STATE로 매핑해 Transition 자동화
	// ---------------------------------------------------------------------
	enum ERenderUsage : uint8
	{
		RENDER_USAGE_SRV = 0,          // shader resource read
		RENDER_USAGE_CBV = 1,          // constant/uniform buffer
		RENDER_USAGE_UAV = 2,          // unordered access
		RENDER_USAGE_RTV = 3,          // render target write
		RENDER_USAGE_DSV_WRITE = 4,    // depth write
		RENDER_USAGE_DSV_READ = 5,     // depth read (as SRV)
		RENDER_USAGE_VERTEX_BUFFER = 6,
		RENDER_USAGE_INDEX_BUFFER = 7,
		RENDER_USAGE_INDIRECT_ARGUMENT = 8,
		RENDER_USAGE_PRESENT = 9,      // swapchain present
	};

	// ---------------------------------------------------------------------
	// One declared access
	// ---------------------------------------------------------------------
	struct RenderPassResourceAccess final
	{
		uint64 ResourceId = 0;
		ERenderResourceKind Kind = RENDER_RESOURCE_KIND_TEXTURE;
		ERenderAccess Access = RENDER_ACCESS_READ;
		ERenderUsage Usage = RENDER_USAGE_SRV;

		// For textures: which view type does this usage imply (optional hint)
		// ex) RTV/DSV/SRV. SRV는 default view로도 충분하지만 명시하면 더 안전.
		TEXTURE_VIEW_TYPE TextureViewType = TEXTURE_VIEW_UNDEFINED;

		// Optional: for external resources (swapchain back buffer etc.)
		// Renderer가 ResourceId로 분기해서 가져오면 되니 지금은 비워둬도 됨.
	};
} // namespace shz
