#pragma once
#include "Primitives/BasicTypes.h"

namespace shz
{
	enum class EAssetStatus : uint8
	{
		Unloaded = 0,   // Not resident and not requested.
		Loading = 1,   // Async request in-flight.
		Loaded = 2,   // Resident and usable.
		Failed = 3    // Load failed; error recorded internally.
	};
} // namespace shz