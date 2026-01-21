#pragma once
#include <unordered_map>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>
#include <algorithm>
#include <atomic>
#include <string>

#include "Primitives/BasicTypes.h"

#include "Engine/AssetRuntime/Common/AssetRef.hpp"
#include "Engine/AssetRuntime/Common/AssetPtr.hpp"
#include "Engine/AssetRuntime/Common/AssetTypeTraits.h"

#include "Engine/AssetRuntime/AssetManager/Public/IAssetManager.h"
#include "Engine/AssetRuntime/AssetManager/Public/AssetRegistry.h"
#include "Engine/AssetRuntime/AssetManager/Public/AssetRecord.h"
#include "Engine/AssetRuntime/AssetManager/Public/AssetMeta.h"

namespace shz
{
	enum class EAssetLoadFlags : uint32
	{
		None = 0,
		HighPriority = 1u << 0,
		KeepResident = 1u << 1,
		AllowFallback = 1u << 2,
	};

	enum class EAssetSaveFlags : uint32
	{
		None = 0,
		HighPriority = 1u << 0,
		Force = 1u << 1, // save even if not dirty
	};

	class AssetManager final : public IAssetManager
	{
	public:
		using LoaderFn = std::function<std::unique_ptr<AssetObject>(
			AssetManager& assetManager,
			const AssetMeta& meta,
			uint64* pOutResidentBytes,
			std::string* pOutError)>;

		using ExporterFn = std::function<bool(
			AssetManager& assetManager,
			const AssetMeta& meta,
			const AssetObject* pObject,
			const std::string& outPath,
			std::string* pOutError)>;

	public:
		AssetManager() = default;
		~AssetManager() { Shutdown(); }
		void Shutdown() noexcept;

		AssetManager(const AssetManager&) = delete;
		AssetManager& operator=(const AssetManager&) = delete;

		template<typename T>
		[[nodiscard]] AssetPtr<T> Acquire(const AssetRef<T>& ref, EAssetLoadFlags flags = EAssetLoadFlags::None)
		{
			ASSERT(ref, "Cannot acquire null AssetRef.");
			AssetPtr<T> ptr(this, ref.GetID());
			this->RequestLoad(ref.GetID(), AssetTypeTraits<T>::TypeID, static_cast<uint32>(flags));
			return ptr;
		}

		template<typename T>
		void Prefetch(const AssetRef<T>& ref, EAssetLoadFlags flags = EAssetLoadFlags::None)
		{
			ASSERT(ref, "Cannot prefetch null AssetRef.");
			this->RequestLoad(ref.GetID(), AssetTypeTraits<T>::TypeID, static_cast<uint32>(flags));
		}

		template<typename T>
		[[nodiscard]] AssetPtr<T> LoadBlocking(const AssetRef<T>& ref, EAssetLoadFlags flags = EAssetLoadFlags::None)
		{
			AssetPtr<T> ptr = Acquire(ref, flags);
			ptr.Wait();
			return ptr;
		}

		template<typename T>
		void MarkDirty(const AssetRef<T>& ref) noexcept
		{
			ASSERT(ref, "Cannot MarkDirty null AssetRef.");
			MarkDirtyByID(ref.GetID(), AssetTypeTraits<T>::TypeID);
		}

		template<typename T>
		void RequestSave(const AssetRef<T>& ref, const std::string& outPath = {}, EAssetSaveFlags flags = EAssetSaveFlags::None)
		{
			ASSERT(ref, "Cannot RequestSave null AssetRef.");
			this->RequestSave(ref.GetID(), AssetTypeTraits<T>::TypeID, outPath, static_cast<uint32>(flags));
		}

		template<typename T>
		void SaveBlocking(const AssetRef<T>& ref, const std::string& outPath = {}, EAssetSaveFlags flags = EAssetSaveFlags::None)
		{
			RequestSave(ref, outPath, flags);
			WaitSaveByID(ref.GetID(), AssetTypeTraits<T>::TypeID);
		}

		// -------------------------
		// Registry
		// -------------------------
		template <typename T>
		AssetRef<T> RegisterAsset(const std::string& sourcePath)
		{
			return AssetRef<T>(RegisterAsset(AssetTypeTraits<T>::TypeID, sourcePath));
		}

		AssetID RegisterAsset(const AssetTypeID typeID, const std::string& sourcePath);
		void UnregisterAsset(const AssetID& id);

		void RegisterImporter(AssetTypeID typeId, LoaderFn loader);
		void RegisterExporter(AssetTypeID typeId, ExporterFn exporter);

		void SetBudgetBytes(uint64 bytes) noexcept { m_BudgetBytes.store(bytes, std::memory_order_relaxed); }
		uint64 GetBudgetBytes() const noexcept { return m_BudgetBytes.load(std::memory_order_relaxed); }

		uint64 GetResidentBytes() const noexcept { return m_ResidentBytes.load(std::memory_order_relaxed); }
		uint64 GetFrameIndex() const noexcept { return m_FrameIndex.load(std::memory_order_relaxed); }

		void MarkDirtyByID(const AssetID& id, AssetTypeID typeId) noexcept;

		// -------------------------
		// IAssetManager
		// -------------------------
		void AddStrongRef(const AssetID& id, AssetTypeID typeId) noexcept override;
		void ReleaseStrongRef(const AssetID& id, AssetTypeID typeId) noexcept override;

		void RequestLoad(const AssetID& id, AssetTypeID typeId, uint32 flags) override;
		void RequestSave(const AssetID& id, AssetTypeID typeId, const std::string& outPath, uint32 flags) override;

		EAssetLoadStatus GetLoadStatusByID(const AssetID& id, AssetTypeID typeId) const noexcept override;
		EAssetSaveStatus GetSaveStatusByID(const AssetID& id, AssetTypeID typeId) const noexcept override;

		AssetObject* TryGetByID(const AssetID& id, AssetTypeID typeId) noexcept override;
		const AssetObject* TryGetByID(const AssetID& id, AssetTypeID typeId) const noexcept override;

		void WaitLoadByID(const AssetID& id, AssetTypeID typeId) const override;
		void WaitSaveByID(const AssetID& id, AssetTypeID typeId) const override;

		// -------------------------
		// Eviction, tick
		// -------------------------
		void SetMaxEvictPerCollect(uint32 n) noexcept { m_MaxEvictPerCollect = n; }
		uint32 GetMaxEvictPerCollect() const noexcept { return m_MaxEvictPerCollect; }

		bool Unload(const AssetID& id);
		void CollectGarbage();
		void Tick(float deltaSeconds);

	private:
		AssetRecord& getOrCreateRecord_NoLock(const AssetID& id, AssetTypeID typeId);
		AssetRecord* getRecordOrNull_NoLock(const AssetID& id) noexcept;
		const AssetRecord* getRecordOrNull_NoLock(const AssetID& id) const noexcept;

		void loadNow(AssetRecord& record);
		void saveNow(AssetRecord& record);

		bool isPinned_NoLock(const AssetRecord& rec) const noexcept;
		bool unloadRecord_NoLock(AssetRecord& rec);

		void touchRecord_NoLock(const AssetRecord& rec) const noexcept;

	private:
		mutable std::mutex m_MapMutex = {};

		AssetRegistry m_Registry = {};
		std::unordered_map<AssetID, std::unique_ptr<AssetRecord>> m_Records = {};
		std::unordered_map<AssetTypeID, LoaderFn> m_Loaders = {};

		// NEW
		std::unordered_map<AssetTypeID, ExporterFn> m_Exporters = {};

		std::atomic<uint64> m_FrameIndex{ 0 };
		std::atomic<uint64> m_BudgetBytes{ 512ull * 1024ull * 1024ull };
		std::atomic<uint64> m_ResidentBytes{ 0 };

		uint32 m_MaxEvictPerCollect = 32;

		// NEW
		std::atomic<bool> m_ShuttingDown{ false };
	};

} // namespace shz
