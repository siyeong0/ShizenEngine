#pragma once
#include "Engine/Core/Math/Public/Common.h"
#include "Engine/Core/Math/Public/Constants.h"

#include "Engine/Core/Math/Public/Vector2.h"
#include "Engine/Core/Math/Public/Vector3.h"
#include "Engine/Core/Math/Public/Vector4.h"

#include "Engine/Core/Math/Public/Matrix2x2.h"
#include "Engine/Core/Math/Public/Matrix3x3.h"
#include "Engine/Core/Math/Public/Matrix4x3.h"
#include "Engine/Core/Math/Public/Matrix4x4.h"

#include "Engine/Core/Math/Public/XVector.h"
#include "Engine/Core/Math/Public/XMatrix.h"

#include "Engine/Core/Math/Public/Quaternion.h"
#include "Engine/Core/Math/Public/Box.h"
#include "Engine/Core/Math/Public/OrientedBox.h"
#include "Engine/Core/Math/Public/Plane.h"
#include "Engine/Core/Math/Public/ViewFrustum.h"

#define MATH_STATIC_ASSERT_PODLIKE(T) \
    static_assert(std::is_standard_layout_v<T>,      #T " must have standard layout."); \
    static_assert(std::is_trivially_copyable_v<T>,   #T " must be trivially copyable."); \
    static_assert(std::is_trivially_destructible_v<T>, #T " must be trivially destructible.")

MATH_STATIC_ASSERT_PODLIKE(shz::Vector2);
MATH_STATIC_ASSERT_PODLIKE(shz::Vector3);
MATH_STATIC_ASSERT_PODLIKE(shz::Vector4);
MATH_STATIC_ASSERT_PODLIKE(shz::Matrix2x2);
MATH_STATIC_ASSERT_PODLIKE(shz::Matrix3x3);
MATH_STATIC_ASSERT_PODLIKE(shz::Matrix4x3);
MATH_STATIC_ASSERT_PODLIKE(shz::Matrix4x4);
MATH_STATIC_ASSERT_PODLIKE(shz::Quaternion);

#undef MATH_STATIC_ASSERT_PODLIKE

namespace shz
{
	using float2 = Vector2;
	using float3 = Vector3;
	using float4 = Vector4;

	using float4x4 = Matrix4x4;
	using float3x3 = Matrix3x3;
	using float2x2 = Matrix2x2;

	constexpr inline uint32 BitInterleave16(uint16 _x, uint16 _y)
	{
		// https://graphics.stanford.edu/~seander/bithacks.html#InterleaveBMN

		// Interleave lower 16 bits of x and y, so the bits of x
		// are in the even positions and bits from y in the odd;
		// x | (y << 1) gets the resulting 32-bit Morton Number.
		// x and y must initially be less than 65536.
		uint32 x = _x;
		uint32 y = _y;

		x = (x | (x << 8u)) & 0x00FF00FFu;
		x = (x | (x << 4u)) & 0x0F0F0F0Fu;
		x = (x | (x << 2u)) & 0x33333333u;
		x = (x | (x << 1u)) & 0x55555555u;

		y = (y | (y << 8u)) & 0x00FF00FFu;
		y = (y | (y << 4u)) & 0x0F0F0F0Fu;
		y = (y | (y << 2u)) & 0x33333333u;
		y = (y | (y << 1u)) & 0x55555555u;

		return x | (y << 1u);
	}

	// Returns the least-significant bit and clears it in the input argument
	template <typename T>
	typename std::enable_if<std::is_integral<T>::value, T>::type ExtractLSB(T& bits)
	{
		if (bits == T{ 0 })
			return 0;

		const T bit = bits & ~(bits - T{ 1 });
		bits &= ~bit;

		return bit;
	}

	// Returns the enum value representing the least-significant bit and clears it in the input argument
	template <typename T>
	typename std::enable_if<std::is_enum<T>::value, T>::type ExtractLSB(T& bits)
	{
		return static_cast<T>(ExtractLSB(reinterpret_cast<typename std::underlying_type<T>::type&>(bits)));
	}

	// Wraps Value to the range [Min, Min + Range)
	template <typename T>
	T WrapToRange(T Value, T Min, T Range)
	{
		ASSERT_EXPR(Range >= 0);
		if (Range <= 0)
			return Min;

		T Result = (Value - Min) % Range;
		if (Result < 0)
			Result += Range;

		return Result + Min;
	}

	template <bool bAllowTouch>
	bool CheckBox2DBox2DOverlap(
		const Vector2& Box0Min,
		const Vector2& Box0Max,
		const Vector2& Box1Min,
		const Vector2& Box1Max)
	{
		ASSERT_EXPR(Box0Max.x >= Box0Min.x && Box0Max.y >= Box0Min.y &&
			Box1Max.x >= Box1Min.x && Box1Max.y >= Box1Min.y);
		if (bAllowTouch)
		{
			return !(Box0Min.x > Box1Max.x || Box1Min.x > Box0Max.x || Box0Min.y > Box1Max.y || Box1Min.y > Box0Max.y);
		}
		else
		{
			return !(Box0Min.x >= Box1Max.x || Box1Min.x >= Box0Max.x || Box0Min.y >= Box1Max.y || Box1Min.y >= Box0Max.y);
		}
	}

} // namespace shz

namespace shz::hash
{
	// ------------------------------------------------------------
	// Mix functions
	// ------------------------------------------------------------
	constexpr uint32_t jenkins_rev_mix32(uint32_t key) noexcept
	{
		key += (key << 12);
		key ^= (key >> 22);
		key += (key << 4);
		key ^= (key >> 9);
		key += (key << 10);
		key ^= (key >> 2);
		key += (key << 7);
		key += (key << 12);
		return key;
	}

	constexpr uint64_t twang_mix64(uint64_t key) noexcept
	{
		key = (~key) + (key << 21);
		key = key ^ (key >> 24);
		key = key + (key << 3) + (key << 8);
		key = key ^ (key >> 14);
		key = key + (key << 2) + (key << 4);
		key = key ^ (key >> 28);
		key = key + (key << 31);
		return key;
	}

	static inline size_t combine(size_t seed, size_t v) noexcept
	{
		// boost::hash_combine style constant; use 64-bit constant even on 32-bit
		seed ^= v + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
		return seed;
	}

	// ------------------------------------------------------------
	// Hash one primitive value by bits (memcpy to avoid UB)
	// ------------------------------------------------------------
	template<class T>
	static inline size_t hash_bits(const T& v) noexcept
	{
		static_assert(std::is_trivially_copyable_v<T>, "hash_bits requires trivially copyable type.");

		if constexpr (sizeof(T) == 8)
		{
			uint64_t x = 0;
			std::memcpy(&x, &v, sizeof(v));
			return static_cast<size_t>(twang_mix64(x));
		}
		else if constexpr (sizeof(T) <= 4)
		{
			uint32_t x = 0;
			std::memcpy(&x, &v, sizeof(v));
			return static_cast<size_t>(jenkins_rev_mix32(x));
		}
		else
		{
			// Fallback: hash raw bytes (for odd sizes, still deterministic)
			const uint8_t* p = reinterpret_cast<const uint8_t*>(&v);
			size_t h = 0;
			for (size_t i = 0; i < sizeof(T); ++i)
			{
				h = combine(h, static_cast<size_t>(p[i]));
			}
			return h;
		}
	}

	// ------------------------------------------------------------
	// Hash multiple primitives
	// ------------------------------------------------------------
	template<class... Ts>
	static inline size_t hash_values(const Ts&... xs) noexcept
	{
		size_t h = 0;

		// fold-expression 대체 (C++11 compatible)
		using expander = int[];
		(void)expander {0, (h = combine(h, hash_bits(xs)), 0)...};

		return h;
	}

	// ------------------------------------------------------------
	// Hash POD-like object by bytes (for simple structs if you want)
	// NOTE: This includes padding bytes. Only use if the type is
	//       fully initialized deterministically.
	// ------------------------------------------------------------
	template<class T>
	static inline size_t hash_pod_bytes(const T& obj) noexcept
	{
		static_assert(std::is_standard_layout_v<T> && std::is_trivially_copyable_v<T>,
			"hash_pod_bytes requires POD-like type.");

		const uint8_t* p = reinterpret_cast<const uint8_t*>(&obj);
		size_t h = 0;

		// Chunk 8 bytes where possible for speed
		size_t i = 0;
		for (; i + 8 <= sizeof(T); i += 8)
		{
			uint64_t x = 0;
			std::memcpy(&x, p + i, 8);
			h = combine(h, static_cast<size_t>(twang_mix64(x)));
		}
		for (; i < sizeof(T); ++i)
		{
			h = combine(h, static_cast<size_t>(p[i]));
		}
		return h;
	}
} // namespace shz:hash

namespace std
{
	template<>
	struct hash<shz::Vector2>
	{
		size_t operator()(const shz::Vector2& v) const noexcept
		{
			return shz::hash::hash_values(v.x, v.y);
		}
	};

	template<>
	struct hash<shz::Vector3>
	{
		size_t operator()(const shz::Vector3& v) const noexcept
		{
			return shz::hash::hash_values(v.x, v.y, v.z);
		}
	};

	template<>
	struct hash<shz::Vector4>
	{
		size_t operator()(const shz::Vector4& v) const noexcept
		{
			return shz::hash::hash_values(v.x, v.y, v.z, v.w);
		}
	};

	template<>
	struct hash<shz::Matrix3x3>
	{
		size_t operator()(const shz::Matrix3x3& m) const noexcept
		{
			return shz::hash::hash_values(
				m._m00, m._m01, m._m02,
				m._m10, m._m11, m._m12,
				m._m20, m._m21, m._m22);
		}
	};

	template<>
	struct hash<shz::Matrix4x4>
	{
		size_t operator()(const shz::Matrix4x4& m) const noexcept
		{
			return shz::hash::hash_values(
				m._m00, m._m01, m._m02, m._m03,
				m._m10, m._m11, m._m12, m._m13,
				m._m20, m._m21, m._m22, m._m23,
				m._m30, m._m31, m._m32, m._m33);
		}
	};

	template<>
	struct hash<shz::Quaternion>
	{
		size_t operator()(const shz::Quaternion& q) const noexcept
		{
			// 필드명이 x,y,z,w 라고 가정 (다르면 맞춰줘)
			return shz::hash::hash_values(q.x, q.y, q.z, q.w);
		}
	};

	template<>
	struct hash<shz::Box>
	{
		size_t operator()(const shz::Box& b) const noexcept
		{
			return shz::hash::hash_values(
				b.Min.x, b.Min.y, b.Min.z,
				b.Max.x, b.Max.y, b.Max.z);
		}
	};
} // namespace std