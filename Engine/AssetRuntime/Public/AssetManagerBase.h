#pragma once
#include "Primitives/BasicTypes.h"
#include "Engine/AssetRuntime/Public/IAssetManager.h"
#include "Engine/AssetRuntime/Public/AssetRef.hpp"
#include "Engine/AssetRuntime/Public/AssetPtr.hpp"

namespace shz
{
	enum class EAssetLoadFlags : uint32
	{
		None = 0,
		HighPriority = 1u << 0,
		KeepResident = 1u << 1,
		AllowFallback = 1u << 2,
	};

	constexpr EAssetLoadFlags operator|(EAssetLoadFlags a, EAssetLoadFlags b) noexcept
	{
		return static_cast<EAssetLoadFlags>(static_cast<uint32>(a) | static_cast<uint32>(b));
	}

	constexpr EAssetLoadFlags operator&(EAssetLoadFlags a, EAssetLoadFlags b) noexcept
	{
		return static_cast<EAssetLoadFlags>(static_cast<uint32>(a) & static_cast<uint32>(b));
	}

	class AssetManagerBase : public IAssetManager
	{
	public:
		virtual ~AssetManagerBase() = default;

		template<typename T>
		[[nodiscard]] AssetPtr<T> Acquire(const AssetRef<T>& ref, EAssetLoadFlags flags = EAssetLoadFlags::None)
		{
			ASSERT(ref, "Cannot acquire null AssetRef.");

			this->RequestLoad(ref.GetID(), AssetTypeTraits<T>::TypeID, static_cast<uint32>(flags));
			return AssetPtr<T>(this, ref.GetID());
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
		EAssetStatus GetStatus(const AssetRef<T>& ref) const noexcept
		{
			ASSERT(ref, "Cannot get status of null AssetRef.");
			return this->GetStatusByID(ref.GetID(), AssetTypeTraits<T>::TypeID);
		}

		template<typename T>
		[[nodiscard]] T* TryGet(const AssetRef<T>& ref) noexcept
		{
			ASSERT(ref, "Cannot TryGet null AssetRef.");

			AssetObject* obj = this->TryGetByID(ref.GetID(), AssetTypeTraits<T>::TypeID);
			if (!obj) return nullptr;

			return AssetObjectCast<T>(obj);
		}

		template<typename T>
		[[nodiscard]] const T* TryGet(const AssetRef<T>& ref) const noexcept
		{
			ASSERT(ref, "Cannot TryGet null AssetRef.");

			const AssetObject* obj = this->TryGetByIDConst(ref.GetID(), AssetTypeTraits<T>::TypeID);
			if (!obj) return nullptr;

			return AssetObjectCast<T>(obj);
		}

		virtual bool Unload(const AssetID& id) = 0;
		virtual void CollectGarbage() = 0;
		virtual void Tick(float deltaSeconds) = 0;
	};
}
