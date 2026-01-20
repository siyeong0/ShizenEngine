#pragma once
#include <type_traits>
#include <concepts>

#include "Primitives/BasicTypes.h"
#include "Engine/AssetRuntime/Common/AssetID.hpp"

namespace shz
{
	// ------------------------------------------------------------
	// AssetObject
	// - Type-erased base class for resident asset instances.
	// - Stored inside AssetRecord, owned by AssetManagerImpl.
	// ------------------------------------------------------------
	class AssetObject
	{
	public:
		AssetObject() noexcept = default;
		virtual ~AssetObject() = default;

		AssetObject(const AssetObject&) = delete;
		AssetObject& operator=(const AssetObject&) = delete;

		virtual AssetTypeID GetTypeID() const noexcept = 0;
	};

	// ------------------------------------------------------------
	// AssetTypeConcept
	// - A concrete asset value type (e.g., StaticMeshAsset, TextureAsset).
	// - IMPORTANT: This should NOT derive from AssetObject.
	// ------------------------------------------------------------
	template<typename T>
	concept AssetTypeConcept = (!std::derived_from<T, AssetObject>) && std::is_object_v<T>;

	// ------------------------------------------------------------
	// TypedAssetObject<T>
	// - Owns a concrete asset instance T.
	// ------------------------------------------------------------
	template<AssetTypeConcept T>
	class TypedAssetObject final : public AssetObject
	{
	public:
		using AssetType = T;

	public:
		TypedAssetObject() noexcept = default;

		explicit TypedAssetObject(T&& value) noexcept(std::is_nothrow_move_constructible_v<T>)
			: m_Value(static_cast<T&&>(value))
		{
		}

		explicit TypedAssetObject(const T& value)
			: m_Value(value)
		{
		}

		AssetTypeID GetTypeID() const noexcept override
		{
			return AssetTypeTraits<T>::TypeID;
		}

		T* Get() noexcept { return &m_Value; }
		const T* Get() const noexcept { return &m_Value; }

	private:
		T m_Value = {};
	};

	// Helper cast (safe by typeId)
	template<AssetTypeConcept T>
	inline T* AssetObjectCast(AssetObject* pObj) noexcept
	{
		ASSERT(pObj != nullptr, "AssetObjectCast: pObj is null.");
		ASSERT(pObj->GetTypeID() == AssetTypeTraits<T>::TypeID, "AssetObjectCast: TypeID mismatch.");

		auto* pTyped = static_cast<TypedAssetObject<T>*>(pObj);
		return pTyped->Get();
	}

	template<AssetTypeConcept T>
	inline const T* AssetObjectCast(const AssetObject* pObj) noexcept
	{
		ASSERT(pObj != nullptr, "AssetObjectCast: pObj is null.");
		ASSERT(pObj->GetTypeID() == AssetTypeTraits<T>::TypeID, "AssetObjectCast: TypeID mismatch.");

		auto* pTyped = static_cast<const TypedAssetObject<T>*>(pObj);
		return pTyped->Get();
	}

} // namespace shz
