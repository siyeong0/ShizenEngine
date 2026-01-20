#pragma once
#include <functional>
#include "Primitives/BasicTypes.h"

namespace shz
{
	// ------------------------------------------------------------
	// Asset type tagging
	// - Map each asset class to an AssetTypeId for debugging/validation.
	// - Optional: specialize AssetTypeTraits<T> for custom types.
	// ------------------------------------------------------------
	using AssetTypeID = uint64;

	template<typename T>
	struct AssetTypeTraits
	{
		// Default: 0 means "unknown/unregistered"
		static constexpr AssetTypeID TypeID = 0;
	};

	// ------------------------------------------------------------
	// AssetID
	// - Stable identifier for an asset (path-independent).
	// - 128-bit GUID style: (Hi, Lo)
	// - Zero means "null".
	// ------------------------------------------------------------
	struct AssetID final
	{
		uint64 Hi = 0;
		uint64 Lo = 0;

		constexpr AssetID() noexcept = default;

		constexpr AssetID(uint64 hi, uint64 lo) noexcept
			: Hi(hi)
			, Lo(lo)
		{
		}

		static constexpr AssetID Null() noexcept { return AssetID{}; }

		constexpr bool IsNull() const noexcept { return (Hi | Lo) == 0; }
		constexpr bool IsValid() const noexcept { return !IsNull(); }
		constexpr explicit operator bool() const noexcept { return IsValid(); }

		friend constexpr bool operator==(const AssetID& a, const AssetID& b) noexcept
		{
			return a.Hi == b.Hi && a.Lo == b.Lo;
		}

		friend constexpr bool operator!=(const AssetID& a, const AssetID& b) noexcept
		{
			return !(a == b);
		}

		// Strict weak ordering (useful for maps)
		friend constexpr bool operator<(const AssetID& a, const AssetID& b) noexcept
		{
			return (a.Hi < b.Hi) || (a.Hi == b.Hi && a.Lo < b.Lo);
		}
	};
}

namespace std
{
	template<>
	struct hash<shz::AssetID>
	{
		size_t operator()(const shz::AssetID& h) const noexcept
		{
			return std::hash<uint64_t>{}(h.Hi) ^ (std::hash<uint64_t>{}(h.Lo) << 1);
		}
	};
}
