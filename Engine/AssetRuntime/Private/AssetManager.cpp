#include "pch.h"
#include "AssetManager.h"

namespace shz
{
	void AssetManager::RegisterAsset(const AssetID& id, AssetTypeID typeId, const std::string& sourcePath)
	{
		AssetRegistry::AssetMeta meta = {};
		meta.TypeID = typeId;
		meta.SourcePath = sourcePath;
		m_Registry.Register(id, meta);
	}

	void AssetManager::UnregisterAsset(const AssetID& id)
	{
		m_Registry.Unregister(id);
	}

	void AssetManager::RegisterLoader(AssetTypeID typeId, LoaderFn loader)
	{
		ASSERT(typeId != 0, "AssetManagerImpl::RegisterLoader: invalid TypeID.");
		ASSERT(static_cast<bool>(loader), "AssetManagerImpl::RegisterLoader: loader is null.");

		std::lock_guard<std::mutex> lock(m_MapMutex);
		m_Loaders[typeId] = static_cast<LoaderFn&&>(loader);
	}

	void AssetManager::AddStrongRef(const AssetID& id, AssetTypeID typeId) noexcept
	{
		ASSERT(id, "AssetManagerImpl::AddStrongRef: invalid AssetID.");
		ASSERT(typeId != 0, "AssetManagerImpl::AddStrongRef: invalid AssetTypeID.");

		std::lock_guard<std::mutex> lock(m_MapMutex);
		AssetRecord& rec = getOrCreateRecord_NoLock(id, typeId);
		rec.StrongRefCount.fetch_add(1, std::memory_order_relaxed);
	}

	void AssetManager::ReleaseStrongRef(const AssetID& id, AssetTypeID typeId) noexcept
	{
		ASSERT(id, "AssetManagerImpl::ReleaseStrongRef: invalid AssetID.");
		ASSERT(typeId != 0, "AssetManagerImpl::ReleaseStrongRef: invalid AssetTypeID.");

		std::lock_guard<std::mutex> lock(m_MapMutex);
		AssetRecord* rec = getRecordOrNull_NoLock(id);
		ASSERT(rec, "AssetManagerImpl::ReleaseStrongRef: record not found.");
		ASSERT(rec->TypeID == typeId, "AssetManagerImpl::ReleaseStrongRef: TypeID mismatch.");

		const uint32 prev = rec->StrongRefCount.fetch_sub(1, std::memory_order_relaxed);
		ASSERT(prev != 0, "AssetManagerImpl::ReleaseStrongRef: StrongRefCount underflow.");
	}

	void AssetManager::RequestLoad(const AssetID& id, AssetTypeID typeId, uint32 flags)
	{
		ASSERT(id, "AssetManagerImpl::RequestLoad: invalid AssetID.");
		ASSERT(typeId != 0, "AssetManagerImpl::RequestLoad: invalid AssetTypeID.");

		AssetRecord* rec = nullptr;

		{
			std::lock_guard<std::mutex> mapLock(m_MapMutex);
			rec = &getOrCreateRecord_NoLock(id, typeId);
		}

		{
			std::unique_lock<std::mutex> lock(rec->Mutex);

			touchRecord_NoLock(*rec);

			if (rec->Status == EAssetStatus::Loaded)
			{
				rec->LoadFlags.fetch_or(flags, std::memory_order_relaxed);
				return;
			}

			if (rec->Status == EAssetStatus::Loading)
			{
				rec->LoadFlags.fetch_or(flags, std::memory_order_relaxed);
				return;
			}

			rec->Status = EAssetStatus::Loading;
			rec->LoadFlags.fetch_or(flags, std::memory_order_relaxed);
			rec->Error.clear();
			rec->Object.reset();
			rec->ResidentBytes = 0;
		}

		loadNow(*rec);

		// loadNow() notifies Cv on completion/failure.
	}

	EAssetStatus AssetManager::GetStatusByID(const AssetID& id, AssetTypeID typeId) const noexcept
	{
		ASSERT(id, "AssetManagerImpl::GetStatusByID: invalid AssetID.");
		ASSERT(typeId != 0, "AssetManagerImpl::GetStatusByID: invalid TypeID.");

		const AssetRecord* rec = nullptr;

		{
			std::lock_guard<std::mutex> lock(m_MapMutex);
			rec = getRecordOrNull_NoLock(id);
		}

		if (!rec)
		{
			return EAssetStatus::Unloaded;
		}

		ASSERT(rec->TypeID == typeId, "AssetManagerImpl::GetStatusByID: TypeID mismatch.");

		std::unique_lock<std::mutex> recLock(rec->Mutex);
		return rec->Status;
	}

	AssetObject* AssetManager::TryGetByID(const AssetID& id, AssetTypeID typeId) noexcept
	{
		ASSERT(id, "AssetManagerImpl::TryGetByID: invalid AssetID.");
		ASSERT(typeId != 0, "AssetManagerImpl::TryGetByID: invalid TypeID.");

		AssetRecord* rec = nullptr;

		{
			std::lock_guard<std::mutex> lock(m_MapMutex);
			rec = getRecordOrNull_NoLock(id);
		}

		if (!rec)
		{
			return nullptr;
		}

		ASSERT(rec->TypeID == typeId, "AssetManagerImpl::TryGetByID: TypeID mismatch.");

		std::unique_lock<std::mutex> lock(rec->Mutex);

		if (rec->Status != EAssetStatus::Loaded || !rec->Object)
		{
			return nullptr;
		}

		touchRecord_NoLock(*rec);
		return rec->Object.get();
	}

	const AssetObject* AssetManager::TryGetByIDConst(const AssetID& id, AssetTypeID typeId) const noexcept
	{
		ASSERT(id, "AssetManagerImpl::TryGetByIDConst: invalid AssetID.");
		ASSERT(typeId != 0, "AssetManagerImpl::TryGetByIDConst: invalid TypeID.");

		const AssetRecord* rec = nullptr;

		{
			std::lock_guard<std::mutex> lock(m_MapMutex);
			rec = getRecordOrNull_NoLock(id);
		}

		if (!rec)
		{
			return nullptr;
		}

		ASSERT(rec->TypeID == typeId, "AssetManagerImpl::TryGetByIDConst: TypeID mismatch.");

		std::unique_lock<std::mutex> lock(rec->Mutex);

		if (rec->Status != EAssetStatus::Loaded || !rec->Object)
		{
			return nullptr;
		}

		touchRecord_NoLock(*rec);
		return rec->Object.get();
	}

	void AssetManager::WaitByID(const AssetID& id, AssetTypeID typeId) const
	{
		ASSERT(id, "AssetManagerImpl::WaitByID: invalid AssetID.");
		ASSERT(typeId != 0, "AssetManagerImpl::WaitByID: invalid TypeID.");

		const AssetRecord* rec = nullptr;

		{
			std::lock_guard<std::mutex> lock(m_MapMutex);
			rec = getRecordOrNull_NoLock(id);
		}

		ASSERT(rec != nullptr, "AssetManagerImpl::WaitByID: record not found.");
		ASSERT(rec->TypeID == typeId, "AssetManagerImpl::WaitByID: TypeID mismatch.");

		std::unique_lock<std::mutex> lock(rec->Mutex);
		rec->Cv.wait(lock, [&]()
			{
				return rec->Status == EAssetStatus::Loaded ||
					rec->Status == EAssetStatus::Failed ||
					rec->Status == EAssetStatus::Unloaded;
			});
	}

	bool AssetManager::Unload(const AssetID& id)
	{
		ASSERT(id, "AssetManagerImpl::Unload: invalid AssetID.");

		AssetRecord* rec = nullptr;

		{
			std::lock_guard<std::mutex> lock(m_MapMutex);

			rec = getRecordOrNull_NoLock(id);
			ASSERT(rec, "AssetManagerImpl::Unload: record not found.");

			if (isPinned_NoLock(*rec))
			{
				ASSERT(false, "AssetManagerImpl::Unload: cannot unload pinned asset.");
				return false;
			}

			if (rec->StrongRefCount.load(std::memory_order_relaxed) != 0)
			{
				ASSERT(false, "AssetManagerImpl::Unload: cannot unload asset with active strong references.");
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

				if (rec->Status != EAssetStatus::Loaded && rec->Status != EAssetStatus::Failed)
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
			ASSERT(existing->TypeID == typeId, "AssetManagerImpl: record TypeID mismatch.");
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
		AssetRegistry::AssetMeta meta = {};
		LoaderFn loader;

		{
			std::lock_guard<std::mutex> lock(m_MapMutex);

			meta = m_Registry.Get(record.ID);
			ASSERT(meta.TypeID == record.TypeID, "AssetManagerImpl::loadNow: registry TypeID mismatch.");

			auto it = m_Loaders.find(meta.TypeID);
			ASSERT(it != m_Loaders.end(), "AssetManagerImpl::loadNow: no loader registered for TypeID.");
			loader = it->second;
		}

		std::unique_ptr<AssetObject> obj;
		std::string err;
		uint64 bytes = 0;

		const bool bOk = loader(meta, obj, bytes, err);

		{
			std::unique_lock<std::mutex> lock(record.Mutex);

			if (!bOk)
			{
				record.Object.reset();
				record.Error = err.empty() ? "Loader failed." : err;
				record.Status = EAssetStatus::Failed;
				record.ResidentBytes = 0;
				record.Cv.notify_all();
				return;
			}

			ASSERT(obj, "AssetManagerImpl::loadNow: loader returned ok but object is null.");
			ASSERT(obj->GetTypeID() == record.TypeID, "AssetManagerImpl::loadNow: loaded object TypeID mismatch.");

			record.Object = static_cast<std::unique_ptr<AssetObject>&&>(obj);
			record.Error.clear();
			record.Status = EAssetStatus::Loaded;

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

	bool AssetManager::isPinned_NoLock(const AssetRecord& rec) const noexcept
	{
		const uint32 flags = rec.LoadFlags.load(std::memory_order_relaxed);
		return (flags & static_cast<uint32>(EAssetLoadFlags::KeepResident)) != 0;
	}

	bool AssetManager::unloadRecord_NoLock(AssetRecord& rec)
	{
		std::unique_lock<std::mutex> lock(rec.Mutex);

		if (rec.Status == EAssetStatus::Unloaded) return false;
		if (rec.StrongRefCount.load(std::memory_order_relaxed) != 0) return false;
		if (isPinned_NoLock(rec)) return false;

		const uint64 bytes = rec.ResidentBytes;

		rec.Object.reset();
		rec.Error.clear();
		rec.Status = EAssetStatus::Unloaded;
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
