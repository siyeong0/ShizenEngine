#pragma once
#include "Primitives/BasicTypes.h"
#include "Primitives/Handle.hpp"

namespace shz
{
	template<typename T>
	class UniqueHandle final
	{
	public:
		UniqueHandle() noexcept = default;

		UniqueHandle(const UniqueHandle&) = delete;
		UniqueHandle& operator=(const UniqueHandle&) = delete;

		UniqueHandle(UniqueHandle&& rhs) noexcept
			: m_Handle(rhs.m_Handle)
		{
			rhs.m_Handle = {};
		}

		UniqueHandle& operator=(UniqueHandle&& rhs) noexcept
		{
			if (this != &rhs)
			{
				Reset();
				m_Handle = rhs.m_Handle;
				rhs.m_Handle = {};
			}
			return *this;
		}

		~UniqueHandle()
		{
			Reset();
		}

		// ------------------------------------------------------------
		// Factory
		// ------------------------------------------------------------
		static UniqueHandle Make()
		{
			return UniqueHandle{ Handle<T>::Create() };
		}

		// ------------------------------------------------------------
		// Observers
		// ------------------------------------------------------------
		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_Handle.IsValid();
		}

		[[nodiscard]] Handle<T> Get() const noexcept
		{
			return m_Handle;
		}

		// ------------------------------------------------------------
		// Ownership
		// - Release(): relinquish ownership without destroying the handle.
		// - Reset(): destroy owned handle (if any).
		// ------------------------------------------------------------
		Handle<T> Release() noexcept
		{
			Handle<T> out = m_Handle;
			m_Handle = {};
			return out;
		}

		void Reset() noexcept
		{
			if (!m_Handle.IsValid())
			{
				return;
			}

			const bool ok = Handle<T>::Destroy(m_Handle);
			ASSERT(ok, "UniqueHandle::Reset() failed. Double-destroy or stale handle detected.");

			m_Handle = {};
		}

	private:
		explicit UniqueHandle(Handle<T> h) noexcept
			: m_Handle(h)
		{
		}

	private:
		Handle<T> m_Handle = {};
	};
} // namespace shz
