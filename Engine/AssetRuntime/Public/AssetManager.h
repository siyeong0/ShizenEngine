#pragma once
#include <vector>
#include <concepts>
#include <unordered_map>
#include <optional>

#include "Primitives/BasicTypes.h"
#include "Primitives/Handle.hpp"
#include "Primitives/UniqueHandle.hpp"

#include "Engine/AssetRuntime/Public/AssetObject.h"
#include "Engine/AssetRuntime/Public/TextureAsset.h"
#include "Engine/AssetRuntime/Public/MaterialAsset.h"
#include "Engine/AssetRuntime/Public/StaticMeshAsset.h"

namespace shz
{
	// ------------------------------------------------------------
	// AssetManager
	// - CPU-side registry/cache for assets (no GPU/RHI dependency).
	// - Slot table (vector) + AssetId -> Handle map
	// - Handle safety:
	//   * slot.Owner.Get() == requested handle (index+generation match)
	// ------------------------------------------------------------
	class AssetManager final
	{
	public:
		AssetManager() = default;
		AssetManager(const AssetManager&) = delete;
		AssetManager& operator=(const AssetManager&) = delete;
		~AssetManager() = default;

		Handle<TextureAsset>    RegisterTexture(const TextureAsset& asset);
		Handle<MaterialAsset>   RegisterMaterial(const MaterialAsset& asset);
		Handle<StaticMeshAsset> RegisterStaticMesh(const StaticMeshAsset& asset);

		const TextureAsset& GetTexture(Handle<TextureAsset> h) const noexcept;
		const MaterialAsset& GetMaterial(Handle<MaterialAsset> h) const noexcept;
		const StaticMeshAsset& GetStaticMesh(Handle<StaticMeshAsset> h) const noexcept;

		const TextureAsset* TryGetTexture(Handle<TextureAsset> h) const noexcept;
		const MaterialAsset* TryGetMaterial(Handle<MaterialAsset> h) const noexcept;
		const StaticMeshAsset* TryGetStaticMesh(Handle<StaticMeshAsset> h) const noexcept;

		Handle<TextureAsset>    FindTextureById(const AssetId& id) const noexcept;
		Handle<MaterialAsset>   FindMaterialById(const AssetId& id) const noexcept;
		Handle<StaticMeshAsset> FindStaticMeshById(const AssetId& id) const noexcept;

		bool RemoveTexture(Handle<TextureAsset> h);
		bool RemoveMaterial(Handle<MaterialAsset> h);
		bool RemoveStaticMesh(Handle<StaticMeshAsset> h);

		void Clear();

		uint32 GetTextureCount() const noexcept { return m_TextureCount; }
		uint32 GetMaterialCount() const noexcept { return m_MaterialCount; }
		uint32 GetStaticMeshCount() const noexcept { return m_StaticMeshCount; }

	private:
		template<AssetTypeConcept AssetType>
		struct AssetSlot final
		{
			UniqueHandle<AssetType>  Owner = {}; // owns handle lifetime
			std::optional<AssetType> Value = {}; // owns asset lifetime (no assignment required)
		};

	private:
		template<AssetTypeConcept AssetType>
		static void EnsureSlotCapacity(uint32 index, std::vector<AssetSlot<AssetType>>& slots)
		{
			if (index >= static_cast<uint32>(slots.size()))
			{
				slots.resize(static_cast<size_t>(index) + 1);
			}
		}

		template<AssetTypeConcept AssetType>
		static AssetSlot<AssetType>* FindSlot(Handle<AssetType> h, std::vector<AssetSlot<AssetType>>& slots) noexcept
		{
			if (!h.IsValid())
			{
				return nullptr;
			}

			const uint32 index = h.GetIndex();
			if (index == 0 || index >= static_cast<uint32>(slots.size()))
			{
				return nullptr;
			}

			auto& slot = slots[index];

			if (!slot.Value.has_value())
			{
				return nullptr;
			}

			// generation-safe check:
			// stale handle(index same, gen different) -> Owner.Get() != h
			if (slot.Owner.Get() != h)
			{
				return nullptr;
			}

			return &slot;
		}

		template<AssetTypeConcept AssetType>
		static const AssetSlot<AssetType>* FindSlot(Handle<AssetType> h, const std::vector<AssetSlot<AssetType>>& slots) noexcept
		{
			if (!h.IsValid())
			{
				return nullptr;
			}

			const uint32 index = h.GetIndex();
			if (index == 0 || index >= static_cast<uint32>(slots.size()))
			{
				return nullptr;
			}

			const auto& slot = slots[index];

			if (!slot.Value.has_value())
			{
				return nullptr;
			}

			if (slot.Owner.Get() != h)
			{
				return nullptr;
			}

			return &slot;
		}

	private:
		template<AssetTypeConcept AssetType>
		static Handle<AssetType> RegisterAsset(
			const AssetType& asset,
			std::vector<AssetSlot<AssetType>>& slots,
			std::unordered_map<AssetId, Handle<AssetType>>& idToHandle,
			uint32& count);

		template<AssetTypeConcept AssetType>
		static const AssetType& GetAsset(
			Handle<AssetType> h,
			const std::vector<AssetSlot<AssetType>>& slots) noexcept;

		template<AssetTypeConcept AssetType>
		static const AssetType* TryGetAsset(
			Handle<AssetType> h,
			const std::vector<AssetSlot<AssetType>>& slots) noexcept;

		template<AssetTypeConcept AssetType>
		static Handle<AssetType> FindById(
			const AssetId& id,
			const std::vector<AssetSlot<AssetType>>& slots,
			const std::unordered_map<AssetId, Handle<AssetType>>& idToHandle) noexcept;

		template<AssetTypeConcept AssetType>
		static bool RemoveAsset(
			Handle<AssetType> h,
			std::vector<AssetSlot<AssetType>>& slots,
			std::unordered_map<AssetId, Handle<AssetType>>& idToHandle,
			uint32& count);

		template<AssetTypeConcept AssetType>
		static void ClearTable(
			std::vector<AssetSlot<AssetType>>& slots,
			std::unordered_map<AssetId, Handle<AssetType>>& idToHandle,
			uint32& count);

	private:
		std::vector<AssetSlot<TextureAsset>> m_TextureSlots;
		std::unordered_map<AssetId, Handle<TextureAsset>> m_TextureIdToHandle;
		uint32 m_TextureCount = 0;

		std::vector<AssetSlot<MaterialAsset>> m_MaterialSlots;
		std::unordered_map<AssetId, Handle<MaterialAsset>> m_MaterialIdToHandle;
		uint32 m_MaterialCount = 0;

		std::vector<AssetSlot<StaticMeshAsset>> m_StaticMeshSlots;
		std::unordered_map<AssetId, Handle<StaticMeshAsset>> m_StaticMeshIdToHandle;
		uint32 m_StaticMeshCount = 0;
	};

} // namespace shz
