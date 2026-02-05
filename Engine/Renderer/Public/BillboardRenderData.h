#pragma once
#include "Primitives/BasicTypes.h"
#include "Engine/Core/Math/Math.h"
#include "Engine/Core/Common/Public/HashUtils.hpp"
#include "Engine/Core/Common/Public/RefCntAutoPtr.hpp"

#include "Engine/RuntimeData/Public/Material.h"

#include "Engine/RHI/Interface/ITexture.h"

#include "Engine/RHI/Interface/IPipelineState.h"
#include "Engine/RHI/Interface/IShaderResourceBinding.h"

namespace shz
{
	struct BillboardRenderData final
	{
		RefCntAutoPtr<ITexture> BaseColorTex;
		MATERIAL_BLEND_MODE BlendMode = MATERIAL_BLEND_MODE_OPAQUE;

		RefCntAutoPtr<IBuffer> VertexBuffer;
		RefCntAutoPtr<IBuffer> IndexBuffer;
	};
} // namespace std