#include "pch.h"
#include "AssetManager.h"

namespace shz
{
	// ------------------------------------------------------------
	// Template implementations
	// ------------------------------------------------------------
	template<AssetTypeConcept AssetType>
	Handle<AssetType> AssetManager::RegisterAsset(
		const AssetType& asset,
		std::vector<AssetSlot<AssetType>>& slots,
		std::unordered_map<AssetId, Handle<AssetType>>& idToHandle,
		uint32& count)
	{
		if (!asset.IsValid())
		{
			return {};
		}

		// NOTE:
		// If AssetObject::GetId() returns by value, this copies AssetId.
		// You can later change it to return const AssetId& to avoid copies.
		const AssetId id = asset.GetId();

		// Deduplicate by AssetId.
		if (auto it = idToHandle.find(id); it != idToHandle.end())
		{
			const Handle<AssetType> existing = it->second;

			// If the mapping is still valid and the slot is alive, return it.
			if (FindSlot<AssetType>(existing, slots) != nullptr)
			{
				return existing;
			}

			// Stale mapping: remove it.
			idToHandle.erase(it);
		}

		// Allocate a new handle and place the asset into the slot table.
		UniqueHandle<AssetType> owner = UniqueHandle<AssetType>::Make();
		const Handle<AssetType> h = owner.Get();
		const uint32 index = h.GetIndex();

		EnsureSlotCapacity<AssetType>(index, slots);

		AssetSlot<AssetType>& slot = slots[index];

		// Safety check: a reused index must not still be occupied.
		ASSERT(!slot.Value.has_value() && !slot.Owner.Get().IsValid(), "Asset slot already occupied.");

		slot.Owner = std::move(owner);
		slot.Value.emplace(asset); // Construct in-place (no assignment needed).

		idToHandle.emplace(id, h);
		++count;

		return h;
	}

	template<AssetTypeConcept AssetType>
	const AssetType& AssetManager::GetAsset(
		Handle<AssetType> h,
		const std::vector<AssetSlot<AssetType>>& slots) noexcept
	{
		const AssetSlot<AssetType>* slot = FindSlot<AssetType>(h, slots);
		ASSERT(slot != nullptr, "Invalid Handle<AssetType>.");
		return slot->Value.value();
	}

	template<AssetTypeConcept AssetType>
	const AssetType* AssetManager::TryGetAsset(
		Handle<AssetType> h,
		const std::vector<AssetSlot<AssetType>>& slots) noexcept
	{
		const AssetSlot<AssetType>* slot = FindSlot<AssetType>(h, slots);
		if (slot == nullptr)
		{
			return nullptr;
		}

		return &slot->Value.value();
	}

	template<AssetTypeConcept AssetType>
	Handle<AssetType> AssetManager::FindById(
		const AssetId& id,
		const std::vector<AssetSlot<AssetType>>& slots,
		const std::unordered_map<AssetId, Handle<AssetType>>& idToHandle) noexcept
	{
		auto it = idToHandle.find(id);
		if (it == idToHandle.end())
		{
			return {};
		}

		const Handle<AssetType> h = it->second;

		// Verify that the slot is still valid (protects against stale handles).
		if (FindSlot<AssetType>(h, slots) == nullptr)
		{
			return {};
		}

		return h;
	}

	template<AssetTypeConcept AssetType>
	bool AssetManager::RemoveAsset(
		Handle<AssetType> h,
		std::vector<AssetSlot<AssetType>>& slots,
		std::unordered_map<AssetId, Handle<AssetType>>& idToHandle,
		uint32& count)
	{
		AssetSlot<AssetType>* slot = FindSlot<AssetType>(h, slots);
		if (slot == nullptr)
		{
			return false;
		}

		const AssetId id = slot->Value->GetId();

		// Remove the reverse mapping if it still points to this handle.
		if (auto it = idToHandle.find(id); it != idToHandle.end())
		{
			if (it->second == h)
			{
				idToHandle.erase(it);
			}
		}

		// Destroy the asset, then destroy the handle (generation bump + recycle).
		slot->Value.reset();
		slot->Owner.Reset();

		ASSERT(count > 0, "Asset count underflow.");
		--count;

		return true;
	}

	template<AssetTypeConcept AssetType>
	void AssetManager::ClearTable(
		std::vector<AssetSlot<AssetType>>& slots,
		std::unordered_map<AssetId, Handle<AssetType>>& idToHandle,
		uint32& count)
	{
		// Destroy all assets and return all handles to the pool.
		for (AssetSlot<AssetType>& s : slots)
		{
			s.Value.reset();
			s.Owner.Reset();
		}

		slots.clear();
		idToHandle.clear();
		count = 0;
	}

	// ------------------------------------------------------------
	// Public API (thin wrappers)
	// ------------------------------------------------------------
	Handle<TextureAsset> AssetManager::RegisterTexture(const TextureAsset& asset)
	{
		return RegisterAsset<TextureAsset>(asset, m_TextureSlots, m_TextureIdToHandle, m_TextureCount);
	}

	Handle<MaterialAsset> AssetManager::RegisterMaterial(const MaterialAsset& asset)
	{
		return RegisterAsset<MaterialAsset>(asset, m_MaterialSlots, m_MaterialIdToHandle, m_MaterialCount);
	}

	Handle<StaticMeshAsset> AssetManager::RegisterStaticMesh(const StaticMeshAsset& asset)
	{
		return RegisterAsset<StaticMeshAsset>(asset, m_StaticMeshSlots, m_StaticMeshIdToHandle, m_StaticMeshCount);
	}

	const TextureAsset& AssetManager::GetTexture(Handle<TextureAsset> h) const noexcept
	{
		return GetAsset<TextureAsset>(h, m_TextureSlots);
	}

	const MaterialAsset& AssetManager::GetMaterial(Handle<MaterialAsset> h) const noexcept
	{
		return GetAsset<MaterialAsset>(h, m_MaterialSlots);
	}

	const StaticMeshAsset& AssetManager::GetStaticMesh(Handle<StaticMeshAsset> h) const noexcept
	{
		return GetAsset<StaticMeshAsset>(h, m_StaticMeshSlots);
	}

	const TextureAsset* AssetManager::TryGetTexture(Handle<TextureAsset> h) const noexcept
	{
		return TryGetAsset<TextureAsset>(h, m_TextureSlots);
	}

	const MaterialAsset* AssetManager::TryGetMaterial(Handle<MaterialAsset> h) const noexcept
	{
		return TryGetAsset<MaterialAsset>(h, m_MaterialSlots);
	}

	const StaticMeshAsset* AssetManager::TryGetStaticMesh(Handle<StaticMeshAsset> h) const noexcept
	{
		return TryGetAsset<StaticMeshAsset>(h, m_StaticMeshSlots);
	}

	Handle<TextureAsset> AssetManager::FindTextureById(const AssetId& id) const noexcept
	{
		return FindById<TextureAsset>(id, m_TextureSlots, m_TextureIdToHandle);
	}

	Handle<MaterialAsset> AssetManager::FindMaterialById(const AssetId& id) const noexcept
	{
		return FindById<MaterialAsset>(id, m_MaterialSlots, m_MaterialIdToHandle);
	}

	Handle<StaticMeshAsset> AssetManager::FindStaticMeshById(const AssetId& id) const noexcept
	{
		return FindById<StaticMeshAsset>(id, m_StaticMeshSlots, m_StaticMeshIdToHandle);
	}

	bool AssetManager::RemoveTexture(Handle<TextureAsset> h)
	{
		return RemoveAsset<TextureAsset>(h, m_TextureSlots, m_TextureIdToHandle, m_TextureCount);
	}

	bool AssetManager::RemoveMaterial(Handle<MaterialAsset> h)
	{
		return RemoveAsset<MaterialAsset>(h, m_MaterialSlots, m_MaterialIdToHandle, m_MaterialCount);
	}

	bool AssetManager::RemoveStaticMesh(Handle<StaticMeshAsset> h)
	{
		return RemoveAsset<StaticMeshAsset>(h, m_StaticMeshSlots, m_StaticMeshIdToHandle, m_StaticMeshCount);
	}

	void AssetManager::Clear()
	{
		ClearTable<TextureAsset>(m_TextureSlots, m_TextureIdToHandle, m_TextureCount);
		ClearTable<MaterialAsset>(m_MaterialSlots, m_MaterialIdToHandle, m_MaterialCount);
		ClearTable<StaticMeshAsset>(m_StaticMeshSlots, m_StaticMeshIdToHandle, m_StaticMeshCount);
	}

} // namespace shz
