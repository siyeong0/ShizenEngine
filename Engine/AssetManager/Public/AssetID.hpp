#pragma once
#include <functional>
#include <filesystem>
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
		AssetTypeTraits() { ASSERT(false, "AssetTypeTraits<T> is not specialized for this type."); }
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
		std::string SourcePath = {};

		constexpr AssetID() noexcept = default;

		AssetID(const AssetTypeID typeID, const std::string& path) noexcept
			: SourcePath(path)
		{
			const std::string relativeSourcePath = std::filesystem::relative(SourcePath).string();

			// Make deterministic asset ID from path.
			const size_t h0 = STRING_HASH(relativeSourcePath);
			const size_t h1 = STRING_HASH(relativeSourcePath + std::to_string(static_cast<uint64>(typeID)));

			Hi = static_cast<uint64>(h0) ^ (static_cast<uint64>(typeID) * 0x9E3779B185EBCA87ull);
			Lo = static_cast<uint64>(h1) ^ (static_cast<uint64>(typeID) * 0xC2B2AE3D27D4EB4Full);
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
