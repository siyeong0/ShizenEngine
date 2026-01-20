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
#include "Engine/AssetRuntime/Common/AssetTypeTraits.h"

#include "Engine/AssetRuntime/AssetManager/Public/AssetManagerBase.h"
#include "Engine/AssetRuntime/AssetManager/Public/AssetRegistry.h"
#include "Engine/AssetRuntime/AssetManager/Public/AssetRecord.h"

namespace shz
{
	class AssetManager final : public AssetManagerBase
	{
	public:
		using LoaderFn = std::function<bool(
			const AssetRegistry::AssetMeta& meta,
			std::unique_ptr<AssetObject>& outObject,
			uint64& outResidentBytes,
			std::string& outError)>;

	public:
		AssetManager() = default;
		~AssetManager() override = default;

		AssetManager(const AssetManager&) = delete;
		AssetManager& operator=(const AssetManager&) = delete;

		template <typename T>
		AssetRef<T> RegisterAsset(const std::string& sourcePath)
		{
			return AssetRef<T>(RegisterAsset(AssetTypeTraits<T>::TypeID, sourcePath));
		}

		AssetID RegisterAsset(const AssetTypeID typeID, const std::string& sourcePath);
		void UnregisterAsset(const AssetID& id);

		void RegisterLoader(AssetTypeID typeId, LoaderFn loader);

		void SetBudgetBytes(uint64 bytes) noexcept { m_BudgetBytes.store(bytes, std::memory_order_relaxed); }
		uint64 GetBudgetBytes() const noexcept { return m_BudgetBytes.load(std::memory_order_relaxed); }

		uint64 GetResidentBytes() const noexcept { return m_ResidentBytes.load(std::memory_order_relaxed); }
		uint64 GetFrameIndex() const noexcept { return m_FrameIndex.load(std::memory_order_relaxed); }

	public:
		// AssetManagerBase
		void AddStrongRef(const AssetID& id, AssetTypeID typeId) noexcept override;
		void ReleaseStrongRef(const AssetID& id, AssetTypeID typeId) noexcept override;

		void RequestLoad(const AssetID& id, AssetTypeID typeId, uint32 flags) override;

		EAssetStatus GetStatusByID(const AssetID& id, AssetTypeID typeId) const noexcept override;

		AssetObject* TryGetByID(const AssetID& id, AssetTypeID typeId) noexcept override;
		const AssetObject* TryGetByIDConst(const AssetID& id, AssetTypeID typeId) const noexcept override;

		void WaitByID(const AssetID& id, AssetTypeID typeId) const override;

		void SetMaxEvictPerCollect(uint32 n) noexcept { m_MaxEvictPerCollect = n; }
		uint32 GetMaxEvictPerCollect() const noexcept { return m_MaxEvictPerCollect; }

	public:
		bool Unload(const AssetID& id) override;
		void CollectGarbage() override;
		void Tick(float deltaSeconds) override;

	private:
		AssetRecord& getOrCreateRecord_NoLock(const AssetID& id, AssetTypeID typeId);
		AssetRecord* getRecordOrNull_NoLock(const AssetID& id) noexcept;
		const AssetRecord* getRecordOrNull_NoLock(const AssetID& id) const noexcept;

		void loadNow(AssetRecord& record);

		bool isPinned_NoLock(const AssetRecord& rec) const noexcept;
		bool unloadRecord_NoLock(AssetRecord& rec);

		void touchRecord_NoLock(const AssetRecord& rec) const noexcept;

	private:
		mutable std::mutex m_MapMutex = {};

		AssetRegistry m_Registry = {};
		std::unordered_map<AssetID, std::unique_ptr<AssetRecord>> m_Records = {};
		std::unordered_map<AssetTypeID, LoaderFn> m_Loaders = {};

		std::atomic<uint64> m_FrameIndex{ 0 };
		std::atomic<uint64> m_BudgetBytes{ 512ull * 1024ull * 1024ull };
		std::atomic<uint64> m_ResidentBytes{ 0 };

		uint32 m_MaxEvictPerCollect = 32;
	};

} // namespace shz
