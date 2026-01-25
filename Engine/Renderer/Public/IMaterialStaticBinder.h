#pragma once
#include "Primitives/BasicTypes.h"
#include "Engine/RHI/Interface/IPipelineState.h"

namespace shz
{
	class IMaterialStaticBinder
	{
	public:
		virtual ~IMaterialStaticBinder() = default;
		virtual bool BindStatics(IPipelineState* pPSO) = 0;
	};

} // namespace shz
