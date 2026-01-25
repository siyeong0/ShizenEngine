#pragma once
#include "Primitives/BasicTypes.h"

namespace shz
{
	enum class EAssetLoadStatus : uint8
	{
		Unloaded = 0,   // Not resident and not requested.
		Loading = 1,   // Async request in-flight.
		Loaded = 2,   // Resident and usable.
		Failed = 3    // Load failed; error recorded internally.
	};

	enum class EAssetSaveStatus : uint8
	{
		Idle = 0,   // no pending work
		Saving,     // in progress
		Saved,      // last save succeeded
		Failed,     // last save failed
	};
} // namespace shz