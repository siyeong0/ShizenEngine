// AssetManager.h
#pragma once
#include <unordered_map>
#include <memory>
#include <type_traits>

#include "Primitives/BasicTypes.h"
#include "Engine/AssetRuntime/Public/AssetId.h"
#include "Engine/AssetRuntime/Public/AssetObject.h"

namespace shz
{
	// ------------------------------------------------------------
	// AssetManager (single map)
	// - Stores ALL assets in one registry: AssetId -> AssetObject
	// - Typed access via custom TypeId (no RTTI).
	//
	// Notes:
	// - Register copies the asset into heap storage (unique_ptr).
	// - AssetId MUST be globally unique and stable for the asset.
	// ------------------------------------------------------------
	class AssetManager final
	{
	public:
		AssetManager() = default;
		AssetManager(const AssetManager&) = delete;
		AssetManager& operator=(const AssetManager&) = delete;
		~AssetManager() = default;

		// --------------------------------------------------------
		// Register (by value copy)
		// - If same AssetId already exists, returns existing id.
		// - Requires: T derives AssetObject
		// --------------------------------------------------------
		template<typename T>
		AssetId Register(const T& asset)
		{
			static_assert(std::is_base_of_v<AssetObject, T>, "T must derive from AssetObject.");

			if (!asset.IsValid())
				return {};

			const AssetId id = asset.GetId();
			if (!id.IsValid())
				return {};

			auto it = m_Assets.find(id);
			if (it != m_Assets.end())
			{
				// Optional: type check (debug-time safety)
				ASSERT(it->second && it->second->GetTypeId() == GetAssetTypeId<T>(),
					"AssetId collision across different asset types.");
				return id;
			}

			m_Assets.emplace(id, std::make_unique<T>(asset));
			return id;
		}

		// --------------------------------------------------------
		// Register (take ownership)
		// - Preferred when loading/parsing assets dynamically.
		// --------------------------------------------------------
		template<typename T>
		AssetId Register(std::unique_ptr<T> pAsset)
		{
			static_assert(std::is_base_of_v<AssetObject, T>, "T must derive from AssetObject.");

			if (!pAsset || !pAsset->IsValid())
				return {};

			const AssetId id = pAsset->GetId();
			if (!id.IsValid())
				return {};

			auto it = m_Assets.find(id);
			if (it != m_Assets.end())
			{
				ASSERT(it->second && it->second->GetTypeId() == GetAssetTypeId<T>(),
					"AssetId collision across different asset types.");
				return id;
			}

			m_Assets.emplace(id, std::move(pAsset));
			return id;
		}

		// --------------------------------------------------------
		// Get (typed) - assert policy
		// --------------------------------------------------------
		template<typename T>
		const T& Get(const AssetId& id) const noexcept
		{
			static_assert(std::is_base_of_v<AssetObject, T>, "T must derive from AssetObject.");

			auto it = m_Assets.find(id);
			ASSERT(it != m_Assets.end() && it->second, "Invalid AssetId.");
			ASSERT(it->second->GetTypeId() == GetAssetTypeId<T>(), "Asset type mismatch.");

			return *static_cast<const T*>(it->second.get());
		}

		template<typename T>
		T& Get(const AssetId& id) noexcept
		{
			static_assert(std::is_base_of_v<AssetObject, T>, "T must derive from AssetObject.");

			auto it = m_Assets.find(id);
			ASSERT(it != m_Assets.end() && it->second, "Invalid AssetId.");
			ASSERT(it->second->GetTypeId() == GetAssetTypeId<T>(), "Asset type mismatch.");

			return *static_cast<T*>(it->second.get());
		}

		// --------------------------------------------------------
		// TryGet (typed) - returns nullptr on failure
		// --------------------------------------------------------
		template<typename T>
		const T* TryGet(const AssetId& id) const noexcept
		{
			static_assert(std::is_base_of_v<AssetObject, T>, "T must derive from AssetObject.");

			auto it = m_Assets.find(id);
			if (it == m_Assets.end() || !it->second)
				return nullptr;

			if (it->second->GetTypeId() != GetAssetTypeId<T>())
				return nullptr;

			return static_cast<const T*>(it->second.get());
		}

		template<typename T>
		T* TryGet(const AssetId& id) noexcept
		{
			static_assert(std::is_base_of_v<AssetObject, T>, "T must derive from AssetObject.");

			auto it = m_Assets.find(id);
			if (it == m_Assets.end() || !it->second)
				return nullptr;

			if (it->second->GetTypeId() != GetAssetTypeId<T>())
				return nullptr;

			return static_cast<T*>(it->second.get());
		}

		// --------------------------------------------------------
		// FindById
		// - In this design, AssetId is already the key.
		// - This returns id if exists, otherwise {}.
		// --------------------------------------------------------
		AssetId FindById(const AssetId& id) const noexcept
		{
			return (m_Assets.find(id) != m_Assets.end()) ? id : AssetId{};
		}

		// --------------------------------------------------------
		// Remove / Clear
		// --------------------------------------------------------
		bool Remove(const AssetId& id)
		{
			return (m_Assets.erase(id) > 0);
		}

		void Clear()
		{
			m_Assets.clear();
		}

		// --------------------------------------------------------
		// Stats
		// - Total count across all asset types.
		// - Typed count requires scanning (O(N)).
		// --------------------------------------------------------
		uint32 GetTotalCount() const noexcept
		{
			return static_cast<uint32>(m_Assets.size());
		}

		template<typename T>
		uint32 GetCount() const noexcept
		{
			static_assert(std::is_base_of_v<AssetObject, T>, "T must derive from AssetObject.");

			const AssetTypeId typeId = GetAssetTypeId<T>();
			uint32 count = 0;
			for (const auto& kv : m_Assets)
			{
				if (kv.second && kv.second->GetTypeId() == typeId)
					++count;
			}
			return count;
		}

	private:
		std::unordered_map<AssetId, std::unique_ptr<AssetObject>> m_Assets;
	};

} // namespace shz
