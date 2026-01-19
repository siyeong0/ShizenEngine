#pragma once
#include "Primitives/BasicTypes.h"
#include "Engine/RHI/Interface/IPipelineState.h"

namespace shz
{
	// ----------------------------------------------------------------------------
	// IMaterialStaticBinder
	//
	// Binds renderer-owned static resources to a PSO created by MaterialRenderData.
	// The binder is responsible for setting PSO static variables (FRAME, tables, etc.).
	// ----------------------------------------------------------------------------
	class IMaterialStaticBinder
	{
	public:
		virtual ~IMaterialStaticBinder() = default;

		// Called right after PSO creation (once per PSO).
		// Implementations should bind static variables that never change per draw:
		// - FRAME_CONSTANTS
		// - g_ObjectTable
		// - Any other renderer-global static resources
		virtual bool BindStatics(IPipelineState* pPSO) = 0;
	};

} // namespace shz
