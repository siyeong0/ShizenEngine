#pragma once
#include "Engine/AssetRuntime/Common/AssetID.hpp"

namespace shz
{
	template<typename T>
	class AssetRef final
	{
	public:
		using AssetType = T;

	public:
		constexpr AssetRef() noexcept = default;

		constexpr explicit AssetRef(const AssetID& id) noexcept
			: m_ID(id)
		{
		}

		constexpr const AssetID& GetID() const noexcept { return m_ID; }

		constexpr bool IsNull() const noexcept { return m_ID.IsNull(); }
		constexpr bool IsValid() const noexcept { return !IsNull(); }
		constexpr explicit operator bool() const noexcept { return IsValid(); }

		friend constexpr bool operator==(const AssetRef& a, const AssetRef& b) noexcept
		{
			return a.m_ID == b.m_ID;
		}

		friend constexpr bool operator!=(const AssetRef& a, const AssetRef& b) noexcept
		{
			return !(a == b);
		}

	private:
		AssetID m_ID = {};
	};
} // namespace shz