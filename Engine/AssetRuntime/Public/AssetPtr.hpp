#pragma once
#include <type_traits>
#include <concepts>

#include "Primitives/BasicTypes.h"
#include "Engine/AssetRuntime/Public/AssetID.hpp"
#include "Engine/AssetRuntime/Public/AssetObject.h"
#include "Engine/AssetRuntime/Public/EAssetStatus.h"
#include "Engine/AssetRuntime/Public/IAssetManager.h"

namespace shz
{
	// ------------------------------------------------------------
	// AssetPtr<T>
	// - Strong reference:
	//   - Keeps asset resident while this object exists (AddRef/Release in manager).
	//   - Provides direct pointer access when loaded.
	// - IMPORTANT:
	//   - AssetPtr may be "pending": valid reference but not loaded yet => Get() == nullptr.
	// ------------------------------------------------------------
	template<typename T>
	class AssetPtr final
	{
	public:
		using AssetType = T;

	public:
		AssetPtr() noexcept = default;

		AssetPtr(IAssetManager* pManager, const AssetID& id) noexcept
			: m_pManager(pManager)
			, m_ID(id)
		{
			addRef();
		}

		AssetPtr(const AssetPtr& rhs) noexcept
			: m_pManager(rhs.m_pManager)
			, m_ID(rhs.m_ID)
		{
			addRef();
		}

		AssetPtr& operator=(const AssetPtr& rhs) noexcept
		{
			if (this == &rhs) return *this;

			release();

			m_pManager = rhs.m_pManager;
			m_ID = rhs.m_ID;

			addRef();
			return *this;
		}

		AssetPtr(AssetPtr&& rhs) noexcept
			: m_pManager(rhs.m_pManager)
			, m_ID(rhs.m_ID)
		{
			rhs.m_pManager = nullptr;
			rhs.m_ID = {};
		}

		AssetPtr& operator=(AssetPtr&& rhs) noexcept
		{
			if (this == &rhs) return *this;

			release();

			m_pManager = rhs.m_pManager;
			m_ID = rhs.m_ID;

			rhs.m_pManager = nullptr;
			rhs.m_ID = {};
			return *this;
		}

		~AssetPtr() noexcept { release(); }

		const AssetID& GetID() const noexcept { return m_ID; }

		bool IsNull() const noexcept { return m_pManager == nullptr || m_ID.IsNull(); }
		explicit operator bool() const noexcept { return !IsNull(); }

		// Returns the asset pointer if loaded; otherwise nullptr.
		T* Get() noexcept
		{
			if (!m_pManager || !m_ID)
			{
				return nullptr;
			}

			AssetObject* obj = m_pManager->TryGetByID(m_ID, AssetTypeTraits<T>::TypeID);
			if (!obj) return nullptr;

			return AssetObjectCast<T>(obj);
		}

		const T* Get() const noexcept
		{
			if (!m_pManager || !m_ID)
			{
				return nullptr;
			}

			const AssetObject* obj = m_pManager->TryGetByIDConst(m_ID, AssetTypeTraits<T>::TypeID);
			if (!obj) return nullptr;

			return AssetObjectCast<T>(obj);
		}

		// Convenience (assert on use)
		T* operator->() noexcept
		{
			T* p = Get();
			ASSERT(p, "Dereferencing unloaded AssetPtr.");
			return p;
		}

		const T* operator->() const noexcept
		{
			const T* p = Get();
			ASSERT(p, "Dereferencing unloaded AssetPtr.");
			return p;
		}

		T& operator* () noexcept
		{
			T* p = Get();
			ASSERT(p, "Dereferencing unloaded AssetPtr.");
			return *p;
		}

		const T& operator* () const noexcept
		{
			const T* p = Get();
			ASSERT(p, "Dereferencing unloaded AssetPtr.");
			return *p;
		}

		EAssetStatus GetStatus() const noexcept
		{
			ASSERT(m_pManager && m_ID, "Getting status from null AssetPtr.");
			return m_pManager->GetStatusByID(m_ID, AssetTypeTraits<T>::TypeID);
		}

		void Wait() const
		{
			ASSERT(m_pManager && m_ID, "Waiting on null AssetPtr.");
			m_pManager->WaitByID(m_ID, AssetTypeTraits<T>::TypeID);
		}

		void Reset() noexcept
		{
			release();
			m_pManager = nullptr;
			m_ID = {};
		}

	private:
		void addRef() noexcept
		{
			if (m_pManager && m_ID)
			{
				m_pManager->AddStrongRef(m_ID, AssetTypeTraits<T>::TypeID);
			}
		}

		void release() noexcept
		{
			if (m_pManager && m_ID)
			{
				m_pManager->ReleaseStrongRef(m_ID, AssetTypeTraits<T>::TypeID);
			}
		}

	private:
		IAssetManager* m_pManager = nullptr;
		AssetID m_ID = {};
	};

} // namespace shz
