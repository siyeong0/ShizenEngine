#pragma once
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <string>

#include "Primitives/BasicTypes.h"
#include "Engine/AssetRuntime/Common/AssetID.hpp"
#include "Engine/AssetRuntime/Common/EAssetStatus.h"
#include "Engine/AssetRuntime/Common/AssetObject.h"

namespace shz
{
	struct AssetRecord final
	{
		AssetID ID = {};
		AssetTypeID TypeID = 0;

		std::atomic<uint32> StrongRefCount = 0;

		// Guarded by Mutex:
		EAssetStatus Status = EAssetStatus::Unloaded;

		// NOTE:
		// LoadFlags is accessed from code paths that may not hold rec.Mutex (policy/pin checks).
		// Therefore it must be atomic to avoid data races.
		std::atomic<uint32> LoadFlags{ 0 };

		// LRU / budget
		mutable std::atomic<uint64> LastUsedFrame = 0; // touched when asset is accessed (TryGet*)
		uint64 LoadedFrame = 0;                        // set when load completes
		uint64 ResidentBytes = 0;                      // estimated residency (0 = unknown)

		std::unique_ptr<AssetObject> Object = nullptr;

		// Optional error for loader failures
		std::string Error = {};

		mutable std::mutex Mutex = {};
		mutable std::condition_variable Cv = {};
	};

} // namespace shz
