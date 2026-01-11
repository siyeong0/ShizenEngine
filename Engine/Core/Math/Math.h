#pragma once
#include "Engine/Core/Math/Public/Common.h"
#include "Engine/Core/Math/Public/Constants.h"

#include "Engine/Core/Math/Public/Vector2.h"
#include "Engine/Core/Math/Public/Vector3.h"
#include "Engine/Core/Math/Public/Vector4.h"

#include "Engine/Core/Math/Public/Matrix3x3.h"
#include "Engine/Core/Math/Public/Matrix4x3.h"
#include "Engine/Core/Math/Public/Matrix4x4.h"

#include "Engine/Core/Math/Public/Quaternion.h"

#include "Engine/Core/Math/Public/XVector.h"
#include "Engine/Core/Math/Public/XMatrix.h"

#define MATH_STATIC_ASSERT_PODLIKE(T) \
    static_assert(std::is_standard_layout_v<T>,      #T " must have standard layout."); \
    static_assert(std::is_trivially_copyable_v<T>,   #T " must be trivially copyable."); \
    static_assert(std::is_trivially_destructible_v<T>, #T " must be trivially destructible.")

MATH_STATIC_ASSERT_PODLIKE(shz::Vector2);
MATH_STATIC_ASSERT_PODLIKE(shz::Vector3);
MATH_STATIC_ASSERT_PODLIKE(shz::Vector4);
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
		VERIFY_EXPR(Range >= 0);
		if (Range <= 0)
			return Min;

		T Result = (Value - Min) % Range;
		if (Result < 0)
			Result += Range;

		return Result + Min;
	}

} // namespace shz

#include "Engine/Core/Common/Public/HashUtils.hpp"
namespace std
{
	template<>
	struct hash<shz::Vector2>
	{
		size_t operator()(const shz::Vector2& v2) const
		{
			return shz::ComputeHash(v2.x, v2.y);
		}
	};
	template<>
	struct hash<shz::Vector3>
	{
		size_t operator()(const shz::Vector3& v3) const
		{
			return shz::ComputeHash(v3.x, v3.y, v3.z);
		}
	};
	template<>
	struct hash<shz::Vector4>
	{
		size_t operator()(const shz::Vector4& v4) const
		{
			return shz::ComputeHash(v4.x, v4.y, v4.z, v4.w);
		}
	};
	template<>
	struct hash<shz::Matrix3x3>
	{
		size_t operator()(const shz::Matrix3x3& m) const
		{
			return shz::ComputeHash(
				m._m00, m._m01, m._m02,
				m._m10, m._m11, m._m12,
				m._m20, m._m21, m._m22);
		}
	};
	template<>
	struct hash<shz::Matrix4x4>
	{
		size_t operator()(const shz::Matrix4x4& m) const
		{
			return shz::ComputeHash(
				m._m00, m._m01, m._m02, m._m03,
				m._m10, m._m11, m._m12, m._m13,
				m._m20, m._m21, m._m22, m._m23,
				m._m30, m._m31, m._m32, m._m33);
		}
	};

} // namespace std