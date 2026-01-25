#pragma once
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <string>

#include "Primitives/BasicTypes.h"
#include "Engine/AssetManager/Public/AssetID.hpp"
#include "Engine/AssetManager/Public/EAssetStatus.h"
#include "Engine/AssetManager/Public/AssetObject.h"

namespace shz
{
	struct AssetRecord final
	{
		AssetID ID = {};
		AssetTypeID TypeID = 0;

		std::atomic<uint32> StrongRefCount = 0;

		// Guarded by Mutex:
		EAssetLoadStatus Status = EAssetLoadStatus::Unloaded;
		std::atomic<uint32> LoadFlags{ 0 };

		EAssetSaveStatus SaveStatus = EAssetSaveStatus::Idle;
		std::atomic<uint32> SaveFlags{ 0 };

		// Set when modified; exporter may clear after successful save
		std::atomic<bool> Dirty{ false };

		// Guarded by Mutex: requested output path.
		// - if empty: use meta.SourcePath during saveNow()
		std::string PendingSavePath = {};

		// Optional error for save failures (guarded by Mutex)
		std::string SaveError = {};

		// Save bookkeeping
		uint64 LastSavedFrame = 0;

		// LRU / budget
		mutable std::atomic<uint64> LastUsedFrame = 0;
		uint64 LoadedFrame = 0;
		uint64 ResidentBytes = 0;

		std::unique_ptr<AssetObject> Object = nullptr;

		// Optional error for loader failures
		std::string Error = {};

		mutable std::mutex Mutex = {};
		mutable std::condition_variable Cv = {};
	};

} // namespace shz
