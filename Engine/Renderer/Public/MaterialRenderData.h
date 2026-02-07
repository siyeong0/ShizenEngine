#pragma once
#include "Primitives/BasicTypes.h"
#include "Engine/Core/Common/Public/RefCntAutoPtr.hpp"
#include "Engine/RHI/Interface/IPipelineState.h"
#include "Engine/RHI/Interface/IShaderResourceBinding.h"

namespace shz
{
	using MaterialRenderDataId = uint64;

	struct MaterialRenderData final
	{
		RefCntAutoPtr<IPipelineState> PipelineState;
		RefCntAutoPtr<IShaderResourceBinding> ShaderResourceBinding;
	};
} // namespace shz