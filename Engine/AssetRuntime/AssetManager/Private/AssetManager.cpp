#include "pch.h"
#include "Engine/AssetRuntime/AssetManager/Public/AssetManager.h"

namespace shz
{
	void AssetManager::Shutdown() noexcept
	{
		const bool already = m_ShuttingDown.exchange(true, std::memory_order_relaxed);
		if (already)
		{
			return;
		}

		std::vector<std::pair<AssetID, AssetTypeID>> records;
		{
			std::lock_guard<std::mutex> lock(m_MapMutex);
			records.reserve(m_Records.size());
			for (auto& kv : m_Records)
			{
				const AssetRecord* rec = kv.second.get();
				records.emplace_back(rec->ID, rec->TypeID);
			}
		}

		for (auto& it : records)
		{
			const AssetID id = it.first;
			const AssetTypeID typeId = it.second;

			AssetRecord* rec = nullptr;
			AssetMeta meta = {};

			{
				std::lock_guard<std::mutex> lock(m_MapMutex);
				rec = getRecordOrNull_NoLock(id);
				if (!rec) continue;

				// meta.SourcePath를 shutdown-save 경로로 사용
				meta = m_Registry.Get(id);
			}

			{
				std::unique_lock<std::mutex> lock(rec->Mutex);
				if (rec->SaveStatus == EAssetSaveStatus::Saving)
				{
					rec->Cv.wait(lock, [&]() { return rec->SaveStatus != EAssetSaveStatus::Saving; });
				}
			}

			const bool dirty = rec->Dirty.load(std::memory_order_relaxed);
			if (dirty)
			{
				const std::string outPath = meta.SourcePath;

				if (!outPath.empty())
				{
					RequestSave(id, typeId, outPath, (uint32)EAssetSaveFlags::None);
				}
				else
				{
					std::unique_lock<std::mutex> lock(rec->Mutex);
					rec->SaveStatus = EAssetSaveStatus::Failed;
					rec->SaveError = "Shutdown: dirty asset has no SourcePath; cannot save.";
					rec->Cv.notify_all();
				}
			}
		}

		for (auto& it : records)
		{
			const AssetID id = it.first;
			const AssetTypeID typeId = it.second;

			AssetRecord* rec = nullptr;
			{
				std::lock_guard<std::mutex> lock(m_MapMutex);
				rec = getRecordOrNull_NoLock(id);
			}
			if (!rec) continue;

			std::unique_lock<std::mutex> lock(rec->Mutex);
			if (rec->SaveStatus == EAssetSaveStatus::Saving)
			{
				rec->Cv.wait(lock, [&]() { return rec->SaveStatus != EAssetSaveStatus::Saving; });
			}
		}
	}

	// ------------------------------------------------------------
	// AssetManager Registry
	// ------------------------------------------------------------

	AssetID AssetManager::RegisterAsset(const AssetTypeID typeID, const std::string& sourcePath)
	{
		ASSERT(typeID != 0, "Invalid AssetTypeID.");
		ASSERT(!sourcePath.empty(), "Path is empty.");

		const AssetID id(typeID, sourcePath);

		ASSERT(id, "Invalid AssetID.");
		ASSERT(typeID != 0, "Invalid AssetTypeID.");

		AssetMeta meta = {};
		meta.TypeID = typeID;
		meta.SourcePath = sourcePath;

		// Registry should be idempotent: override/update meta if already exists.
		m_Registry.Register(id, meta);

		return id;
	}

	void AssetManager::UnregisterAsset(const AssetID& id)
	{
		m_Registry.Unregister(id);
	}

	void AssetManager::RegisterImporter(AssetTypeID typeId, LoaderFn loader)
	{
		ASSERT(typeId != 0, "Invalid TypeID.");
		ASSERT(static_cast<bool>(loader), "Loader is null.");

		std::lock_guard<std::mutex> lock(m_MapMutex);
		m_Loaders[typeId] = static_cast<LoaderFn&&>(loader);
	}

	void AssetManager::RegisterExporter(AssetTypeID typeId, ExporterFn exporter)
	{
		ASSERT(typeId != 0, "Invalid AssetTypeID.");
		ASSERT((bool)exporter, "Exporter is empty.");

		std::lock_guard<std::mutex> lock(m_MapMutex);
		m_Exporters[typeId] = std::move(exporter);
	}

	void AssetManager::MarkDirtyByID(const AssetID& id, AssetTypeID typeId) noexcept
	{
		ASSERT(id, "Invalid AssetID.");
		ASSERT(typeId != 0, "Invalid AssetTypeID.");

		AssetRecord* rec = nullptr;
		{
			std::lock_guard<std::mutex> lock(m_MapMutex);
			rec = getRecordOrNull_NoLock(id);
		}

		if (!rec)
		{
			return;
		}
		ASSERT(rec->TypeID == typeId, "TypeID mismatch.");

		rec->Dirty.store(true, std::memory_order_relaxed);
	}

	// ------------------------------------------------------------
	// IAssetManager
	// ------------------------------------------------------------

	void AssetManager::AddStrongRef(const AssetID& id, AssetTypeID typeId) noexcept
	{
		ASSERT(id, "Invalid AssetID.");
		ASSERT(typeId != 0, "Invalid AssetTypeID.");

		std::lock_guard<std::mutex> lock(m_MapMutex);
		AssetRecord& rec = getOrCreateRecord_NoLock(id, typeId);
		rec.StrongRefCount.fetch_add(1, std::memory_order_relaxed);
	}

	void AssetManager::ReleaseStrongRef(const AssetID& id, AssetTypeID typeId) noexcept
	{
		ASSERT(id, "Invalid AssetID.");
		ASSERT(typeId != 0, "Invalid AssetTypeID.");

		std::lock_guard<std::mutex> lock(m_MapMutex);
		AssetRecord* rec = getRecordOrNull_NoLock(id);
		ASSERT(rec, "Record not found.");
		ASSERT(rec->TypeID == typeId, "TypeID mismatch.");

		const uint32 prev = rec->StrongRefCount.fetch_sub(1, std::memory_order_relaxed);
		ASSERT(prev != 0, "StrongRefCount underflow.");
	}

	void AssetManager::RequestLoad(const AssetID& id, AssetTypeID typeId, uint32 flags)
	{
		ASSERT(id, "Invalid AssetID.");
		ASSERT(typeId != 0, "Invalid AssetTypeID.");

		AssetRecord* rec = nullptr;

		{
			std::lock_guard<std::mutex> mapLock(m_MapMutex);
			rec = &getOrCreateRecord_NoLock(id, typeId);
		}

		{
			std::unique_lock<std::mutex> lock(rec->Mutex);

			touchRecord_NoLock(*rec);

			if (rec->Status == EAssetLoadStatus::Loaded)
			{
				rec->LoadFlags.fetch_or(flags, std::memory_order_relaxed);
				return;
			}

			if (rec->Status == EAssetLoadStatus::Loading)
			{
				rec->LoadFlags.fetch_or(flags, std::memory_order_relaxed);
				return;
			}

			rec->Status = EAssetLoadStatus::Loading;
			rec->LoadFlags.fetch_or(flags, std::memory_order_relaxed);
			rec->Error.clear();
			rec->Object.reset();
			rec->ResidentBytes = 0;
		}

		loadNow(*rec);

		// loadNow() notifies Cv on completion/failure.
	}

	void AssetManager::RequestSave(const AssetID& id, AssetTypeID typeId, const std::string& outPath, uint32 flags)
	{
		ASSERT(id, "Invalid AssetID.");
		ASSERT(typeId != 0, "Invalid AssetTypeID.");

		if (m_ShuttingDown.load(std::memory_order_relaxed))
		{
			// shutdown 중에도 저장은 허용(오히려 필요)
		}

		AssetRecord* rec = nullptr;

		{
			std::lock_guard<std::mutex> mapLock(m_MapMutex);
			rec = &getOrCreateRecord_NoLock(id, typeId);
		}

		{
			std::unique_lock<std::mutex> lock(rec->Mutex);

			if (rec->Status == EAssetLoadStatus::Loading)
			{
				rec->Cv.wait(lock, [&]() { return rec->Status != EAssetLoadStatus::Loading; });
			}

			ASSERT(rec->TypeID == typeId, "TypeID mismatch.");

			if (rec->Status != EAssetLoadStatus::Loaded || !rec->Object)
			{
				rec->SaveStatus = EAssetSaveStatus::Failed;
				rec->SaveError = "RequestSave: asset is not loaded.";
				rec->Cv.notify_all();
				return;
			}

			if (rec->SaveStatus == EAssetSaveStatus::Saving)
			{
				rec->Cv.wait(lock, [&]() { return rec->SaveStatus != EAssetSaveStatus::Saving; });
			}

			const bool force = (flags & (uint32)EAssetSaveFlags::Force) != 0;
			if (!force && !rec->Dirty.load(std::memory_order_relaxed))
			{
				rec->SaveStatus = EAssetSaveStatus::Idle;
				rec->SaveError.clear();
				rec->Cv.notify_all();
				return;
			}

			rec->SaveStatus = EAssetSaveStatus::Saving;
			rec->SaveFlags.fetch_or(flags, std::memory_order_relaxed);
			rec->SaveError.clear();

			rec->PendingSavePath = outPath; // empty allowed => meta.SourcePath
		}

		saveNow(*rec);

		// saveNow() will notify Cv.
	}

	EAssetLoadStatus AssetManager::GetLoadStatusByID(const AssetID& id, AssetTypeID typeId) const noexcept
	{
		ASSERT(id, "Invalid AssetID.");
		ASSERT(typeId != 0, "Invalid TypeID.");

		const AssetRecord* rec = nullptr;

		{
			std::lock_guard<std::mutex> lock(m_MapMutex);
			rec = getRecordOrNull_NoLock(id);
		}

		if (!rec)
		{
			return EAssetLoadStatus::Unloaded;
		}

		ASSERT(rec->TypeID == typeId, "TypeID mismatch.");

		std::unique_lock<std::mutex> recLock(rec->Mutex);
		return rec->Status;
	}

	EAssetSaveStatus AssetManager::GetSaveStatusByID(const AssetID& id, AssetTypeID typeId) const noexcept
	{
		ASSERT(id, "Invalid AssetID.");
		ASSERT(typeId != 0, "Invalid TypeID.");

		const AssetRecord* rec = nullptr;
		{
			std::lock_guard<std::mutex> lock(m_MapMutex);
			rec = getRecordOrNull_NoLock(id);
		}

		if (!rec)
		{
			return EAssetSaveStatus::Idle;
		}

		ASSERT(rec->TypeID == typeId, "TypeID mismatch.");

		std::lock_guard<std::mutex> lock(rec->Mutex);
		return rec->SaveStatus;
	}

	AssetObject* AssetManager::TryGetByID(const AssetID& id, AssetTypeID typeId) noexcept
	{
		ASSERT(id, "Invalid AssetID.");
		ASSERT(typeId != 0, "Invalid TypeID.");

		AssetRecord* rec = nullptr;

		{
			std::lock_guard<std::mutex> lock(m_MapMutex);
			rec = getRecordOrNull_NoLock(id);
		}

		if (!rec)
		{
			return nullptr;
		}

		ASSERT(rec->TypeID == typeId, "TypeID mismatch.");

		std::unique_lock<std::mutex> lock(rec->Mutex);

		if (rec->Status != EAssetLoadStatus::Loaded || !rec->Object)
		{
			return nullptr;
		}

		touchRecord_NoLock(*rec);
		return rec->Object.get();
	}

	const AssetObject* AssetManager::TryGetByID(const AssetID& id, AssetTypeID typeId) const noexcept
	{
		ASSERT(id, "Invalid AssetID.");
		ASSERT(typeId != 0, "Invalid TypeID.");

		const AssetRecord* rec = nullptr;

		{
			std::lock_guard<std::mutex> lock(m_MapMutex);
			rec = getRecordOrNull_NoLock(id);
		}

		if (!rec)
		{
			return nullptr;
		}

		ASSERT(rec->TypeID == typeId, "TypeID mismatch.");

		std::unique_lock<std::mutex> lock(rec->Mutex);

		if (rec->Status != EAssetLoadStatus::Loaded || !rec->Object)
		{
			return nullptr;
		}

		touchRecord_NoLock(*rec);
		return rec->Object.get();
	}

	void AssetManager::WaitLoadByID(const AssetID& id, AssetTypeID typeId) const
	{
		ASSERT(id, "Invalid AssetID.");
		ASSERT(typeId != 0, "Invalid TypeID.");

		const AssetRecord* rec = nullptr;

		{
			std::lock_guard<std::mutex> lock(m_MapMutex);
			rec = getRecordOrNull_NoLock(id);
		}

		ASSERT(rec != nullptr, "Record not found.");
		ASSERT(rec->TypeID == typeId, "TypeID mismatch.");

		std::unique_lock<std::mutex> lock(rec->Mutex);
		rec->Cv.wait(lock, [&]()
			{
				return rec->Status == EAssetLoadStatus::Loaded ||
					rec->Status == EAssetLoadStatus::Failed ||
					rec->Status == EAssetLoadStatus::Unloaded;
			});
	}

	void AssetManager::WaitSaveByID(const AssetID& id, AssetTypeID typeId) const
	{
		ASSERT(id, "Invalid AssetID.");
		ASSERT(typeId != 0, "Invalid TypeID.");

		const AssetRecord* rec = nullptr;

		{
			std::lock_guard<std::mutex> lock(m_MapMutex);
			rec = getRecordOrNull_NoLock(id);
		}

		ASSERT(rec != nullptr, "Record not found.");
		ASSERT(rec->TypeID == typeId, "TypeID mismatch.");

		std::unique_lock<std::mutex> lock(rec->Mutex);
		rec->Cv.wait(lock, [&]()
			{
				return rec->SaveStatus == EAssetSaveStatus::Idle ||
					rec->SaveStatus == EAssetSaveStatus::Saved ||
					rec->SaveStatus == EAssetSaveStatus::Failed;
			});
	}

	bool AssetManager::Unload(const AssetID& id)
	{
		ASSERT(id, "Invalid AssetID.");

		AssetRecord* rec = nullptr;

		{
			std::lock_guard<std::mutex> lock(m_MapMutex);

			rec = getRecordOrNull_NoLock(id);
			ASSERT(rec, "Record not found.");

			if (isPinned_NoLock(*rec))
			{
				ASSERT(false, "Cannot unload pinned asset.");
				return false;
			}

			if (rec->StrongRefCount.load(std::memory_order_relaxed) != 0)
			{
				ASSERT(false, "Cannot unload asset with active strong references.");
				return false;
			}

			(void)unloadRecord_NoLock(*rec);
		}

		return true;
	}

	void AssetManager::CollectGarbage()
	{
		if (m_ResidentBytes.load(std::memory_order_relaxed) <= m_BudgetBytes.load(std::memory_order_relaxed))
		{
			return;
		}

		std::vector<AssetRecord*> candidates;
		candidates.reserve(m_Records.size());

		{
			std::lock_guard<std::mutex> lock(m_MapMutex);

			for (auto& kv : m_Records)
			{
				AssetRecord* rec = kv.second.get();

				if (rec->StrongRefCount.load(std::memory_order_relaxed) != 0)
				{
					continue;
				}

				if (isPinned_NoLock(*rec))
				{
					continue;
				}

				std::unique_lock<std::mutex> recLock(rec->Mutex);

				if (rec->Status != EAssetLoadStatus::Loaded && rec->Status != EAssetLoadStatus::Failed)
				{
					continue;
				}

				candidates.push_back(rec);
			}
		}

		std::sort(candidates.begin(), candidates.end(),
			[](const AssetRecord* a, const AssetRecord* b)
			{
				const uint64 la = a->LastUsedFrame.load(std::memory_order_relaxed);
				const uint64 lb = b->LastUsedFrame.load(std::memory_order_relaxed);
				return la < lb;
			});

		uint32 evictedCount = 0;

		{
			std::lock_guard<std::mutex> lock(m_MapMutex);

			for (AssetRecord* rec : candidates)
			{
				if (m_ResidentBytes.load(std::memory_order_relaxed) <= m_BudgetBytes.load(std::memory_order_relaxed))
				{
					break;
				}

				if (evictedCount >= m_MaxEvictPerCollect)
				{
					break;
				}

				if (unloadRecord_NoLock(*rec))
				{
					++evictedCount;
				}
			}
		}
	}

	void AssetManager::Tick(float /*deltaSeconds*/)
	{
		const uint64 frame = m_FrameIndex.fetch_add(1, std::memory_order_relaxed) + 1;

		if ((frame % 60) == 0)
		{
			if (m_ResidentBytes.load(std::memory_order_relaxed) > m_BudgetBytes.load(std::memory_order_relaxed))
			{
				CollectGarbage();
			}
		}
	}

	// ------------------------------------------------------------
	// Private methods
	// ------------------------------------------------------------

	AssetRecord* AssetManager::getRecordOrNull_NoLock(const AssetID& id) noexcept
	{
		auto it = m_Records.find(id);
		return (it != m_Records.end()) ? it->second.get() : nullptr;
	}

	const AssetRecord* AssetManager::getRecordOrNull_NoLock(const AssetID& id) const noexcept
	{
		auto it = m_Records.find(id);
		return (it != m_Records.end()) ? it->second.get() : nullptr;
	}

	AssetRecord& AssetManager::getOrCreateRecord_NoLock(const AssetID& id, AssetTypeID typeId)
	{
		if (AssetRecord* existing = getRecordOrNull_NoLock(id))
		{
			ASSERT(existing->TypeID == typeId, "Record TypeID mismatch.");
			return *existing;
		}

		auto rec = std::make_unique<AssetRecord>();
		rec->ID = id;
		rec->TypeID = typeId;
		rec->StrongRefCount.store(0, std::memory_order_relaxed);
		rec->LastUsedFrame.store(0, std::memory_order_relaxed);
		rec->LoadFlags.store(0, std::memory_order_relaxed);

		AssetRecord* pOut = rec.get();
		m_Records.emplace(id, static_cast<std::unique_ptr<AssetRecord>&&>(rec));
		return *pOut;
	}

	void AssetManager::loadNow(AssetRecord& record)
	{
		AssetMeta meta = {};
		LoaderFn loader;

		{
			std::lock_guard<std::mutex> lock(m_MapMutex);

			meta = m_Registry.Get(record.ID);
			ASSERT(meta.TypeID == record.TypeID, "Registry TypeID mismatch.");

			auto it = m_Loaders.find(meta.TypeID);
			ASSERT(it != m_Loaders.end(), "No loader registered for TypeID.");
			loader = it->second;
		}

		std::string err;
		uint64 bytes = 0;

		std::unique_ptr<AssetObject> obj = loader(*this, meta, &bytes, &err);
		ASSERT(obj, "Load failed.");

		{
			std::unique_lock<std::mutex> lock(record.Mutex);

			ASSERT(obj, "Loader returned ok but object is null.");
			ASSERT(obj->GetTypeID() == record.TypeID, "Loaded object TypeID mismatch.");

			record.Object = static_cast<std::unique_ptr<AssetObject>&&>(obj);
			record.Error.clear();
			record.Status = EAssetLoadStatus::Loaded;

			record.ResidentBytes = bytes;
			record.LoadedFrame = m_FrameIndex.load(std::memory_order_relaxed);

			const uint64 frame = m_FrameIndex.load(std::memory_order_relaxed);
			record.LastUsedFrame.store(frame, std::memory_order_relaxed);

			record.Cv.notify_all();
		}

		if (bytes != 0)
		{
			m_ResidentBytes.fetch_add(bytes, std::memory_order_relaxed);
		}
	}

	void AssetManager::saveNow(AssetRecord& record)
	{
		AssetMeta meta = {};
		ExporterFn exporter;

		std::string outPath;
		const AssetObject* obj = nullptr;

		{
			std::lock_guard<std::mutex> lock(m_MapMutex);

			meta = m_Registry.Get(record.ID);
			ASSERT(meta.TypeID == record.TypeID, "Registry TypeID mismatch.");

			auto it = m_Exporters.find(meta.TypeID);
			ASSERT(it != m_Exporters.end(), "No exporter registered for TypeID.");
			exporter = it->second;
		}

		{
			std::unique_lock<std::mutex> lock(record.Mutex);

			if (record.Status != EAssetLoadStatus::Loaded || !record.Object)
			{
				record.SaveStatus = EAssetSaveStatus::Failed;
				record.SaveError = "saveNow: asset not loaded.";
				record.Cv.notify_all();
				return;
			}

			obj = record.Object.get();

			outPath = record.PendingSavePath.empty() ? meta.SourcePath : record.PendingSavePath;
			if (outPath.empty())
			{
				record.SaveStatus = EAssetSaveStatus::Failed;
				record.SaveError = "saveNow: no output path (PendingSavePath and meta.SourcePath are empty).";
				record.Cv.notify_all();
				return;
			}
		}

		std::string err;
		const bool ok = exporter(*this, meta, obj, outPath, &err);

		{
			std::unique_lock<std::mutex> lock(record.Mutex);

			if (!ok)
			{
				record.SaveStatus = EAssetSaveStatus::Failed;
				record.SaveError = err.empty() ? "saveNow: exporter failed." : err;
			}
			else
			{
				record.SaveStatus = EAssetSaveStatus::Saved;
				record.SaveError.clear();
				record.Dirty.store(false, std::memory_order_relaxed);

				record.LastSavedFrame = m_FrameIndex.load(std::memory_order_relaxed);
			}

			record.PendingSavePath.clear();

			record.Cv.notify_all();
		}
	}

	bool AssetManager::isPinned_NoLock(const AssetRecord& rec) const noexcept
	{
		const uint32 flags = rec.LoadFlags.load(std::memory_order_relaxed);
		return (flags & static_cast<uint32>(EAssetLoadFlags::KeepResident)) != 0;
	}

	bool AssetManager::unloadRecord_NoLock(AssetRecord& rec)
	{
		std::unique_lock<std::mutex> lock(rec.Mutex);

		if (rec.Status == EAssetLoadStatus::Unloaded)
		{
			return false;
		}

		if (rec.StrongRefCount.load(std::memory_order_relaxed) != 0)
		{
			return false;
		}

		if (isPinned_NoLock(rec))
		{
			return false;
		}

		const uint64 bytes = rec.ResidentBytes;

		rec.Object.reset();
		rec.Error.clear();
		rec.Status = EAssetLoadStatus::Unloaded;
		rec.ResidentBytes = 0;
		rec.Cv.notify_all();

		if (bytes != 0)
		{
			m_ResidentBytes.fetch_sub(bytes, std::memory_order_relaxed);
		}

		return true;
	}

	void AssetManager::touchRecord_NoLock(const AssetRecord& rec) const noexcept
	{
		const uint64 frame = m_FrameIndex.load(std::memory_order_relaxed);
		rec.LastUsedFrame.store(frame, std::memory_order_relaxed);
	}

} // namespace shz
