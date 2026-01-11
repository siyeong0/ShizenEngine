#pragma once
#include "Engine/Core/Math/Public/Constants.h"
#undef max
#include <limits>

namespace shz
{
	struct Vector2 final
	{
		float32 x;
		float32 y;

		constexpr Vector2() : x(0.0f), y(0.0f) {}
		constexpr Vector2(float32 x, float32 y) : x(x), y(y) {}
		constexpr Vector2(const Vector2& other) = default;
		Vector2& operator=(const Vector2& other) = default;
		~Vector2() = default;

		inline float32& operator[](size_t idx)
		{
			ASSERT(idx < 2, "FVector2 index out of range.");
			return (&x)[idx];
		}
		inline const float32& operator[](size_t idx) const
		{
			ASSERT(idx < 2, "FVector2 index out of range.");
			return (&x)[idx];
		}

		// Common constants
		static inline constexpr Vector2 Zero() { return Vector2{ 0.f, 0.f }; }
		static inline constexpr Vector2 One() { return Vector2{ 1.f, 1.f }; }
		static inline constexpr Vector2 UnitX() { return Vector2{ 1.f, 0.f }; }
		static inline constexpr Vector2 UnitY() { return Vector2{ 0.f, 1.f }; }

		static inline Vector2 FMaxValue() { return Vector2{ std::numeric_limits<float32>::max(), std::numeric_limits<float32>::max() }; }
		static inline Vector2 FMinValue() { return Vector2{ std::numeric_limits<float32>::lowest(), std::numeric_limits<float32>::lowest() }; }

		// Basic operations
		inline float32 Dot(const Vector2& other) const { return x * other.x + y * other.y; }
		inline float32 SqrMagnitude() const { return Dot(*this); }
		inline float32 Magnitude() const { return std::sqrt(SqrMagnitude()); }
		inline float32 Length() const { return Magnitude(); }
		inline Vector2 Normalized() const
		{
			const float32 lenSq = SqrMagnitude();
			ASSERT(lenSq > 1e-16f, "Attempted to normalize a vector with zero length.");
			return *this / std::sqrt(lenSq);
		}
		inline void Normalize() { *this = Normalized(); }

		// Compound assignments
		inline Vector2& operator+=(const Vector2& other) { *this = *this + other; return *this; }
		inline Vector2& operator-=(const Vector2& other) { *this = *this - other; return *this; }
		inline Vector2& operator*=(const Vector2& other) { *this = *this * other; return *this; }
		inline Vector2& operator/=(const Vector2& other) { *this = *this / other; return *this; }
		inline Vector2& operator*=(float32 v) { *this = *this * v; return *this; }
		inline Vector2& operator/=(float32 v) { *this = *this / v; return *this; }

		// Static helpers
		static inline float32 Dot(const Vector2& a, const Vector2& b) { return a.Dot(b); }
		static inline float32 Magnitude(const Vector2& v) { return v.Magnitude(); }
		static inline float32 SqrMagnitude(const Vector2& v) { return v.SqrMagnitude(); }
		static inline float32 Length(const Vector2& v) { return v.Length(); }
		static inline Vector2 Normalize(const Vector2& v) { return v.Normalized(); }

		static inline Vector2 Abs(const Vector2& v) { return Vector2{ std::fabs(v.x), std::fabs(v.y) }; }
		static inline Vector2 Min(const Vector2& a, const Vector2& b) { return Vector2{ std::fmin(a.x, b.x), std::fmin(a.y, b.y) }; }
		static inline Vector2 Max(const Vector2& a, const Vector2& b) { return Vector2{ std::fmax(a.x, b.x), std::fmax(a.y, b.y) }; }
		static inline float32 MinComponent(const Vector2& v) { return std::fmin(v.x, v.y); }
		static inline float32 MaxComponent(const Vector2& v) { return std::fmax(v.x, v.y); }

		static inline Vector2 Clamp(const Vector2& value, float32 min, float32 max)
		{
			return Clamp(value, Vector2{ min, min }, Vector2{ max, max });
		}
		static inline Vector2 Clamp(const Vector2& value, const Vector2& min, const Vector2& max)
		{
			return Vector2{
				std::fmax(min.x, std::fmin(value.x, max.x)),
				std::fmax(min.y, std::fmin(value.y, max.y))
			};
		}

		static inline Vector2 Lerp(const Vector2& a, const Vector2& b, float32 t)
		{
			return Vector2{
				a.x + (b.x - a.x) * t,
				a.y + (b.y - a.y) * t
			};
		}

		static inline Vector2 SmoothStep(const Vector2& a, const Vector2& b, float32 t)
		{
			t = std::fmax(0.f, std::fmin(t, 1.f)); // keep clamped version
			const float32 s = t * t * (3.f - 2.f * t);
			return Vector2{
				a.x + (b.x - a.x) * s,
				a.y + (b.y - a.y) * s
			};
		}

		// Unary / binary operators
		inline Vector2 operator-() const { return Vector2{ -x, -y }; }

		inline Vector2 operator+(const Vector2& rhs) const { return Vector2{ x + rhs.x, y + rhs.y }; }
		inline Vector2 operator-(const Vector2& rhs) const { return Vector2{ x - rhs.x, y - rhs.y }; }
		inline Vector2 operator*(const Vector2& rhs) const { return Vector2{ x * rhs.x, y * rhs.y }; }
		inline Vector2 operator/(const Vector2& rhs) const { return Vector2{ x / rhs.x, y / rhs.y }; }

		inline Vector2 operator+(float32 s) const { return Vector2{ x + s, y + s }; }
		inline Vector2 operator-(float32 s) const { return Vector2{ x - s, y - s }; }
		inline Vector2 operator*(float32 s) const { return Vector2{ x * s, y * s }; }
		inline Vector2 operator/(float32 s) const { return Vector2{ x / s, y / s }; }

		inline bool operator==(const Vector2& rhs) const { return (x == rhs.x) && (y == rhs.y); }
		inline bool operator!=(const Vector2& rhs) const { return !(*this == rhs); }

		friend inline Vector2 operator*(float32 s, const Vector2& v) { return Vector2{ s * v.x, s * v.y }; }
	};
	static_assert(sizeof(Vector2) == sizeof(float32) * 2, "Vector2 size mismatch");
	static_assert(alignof(Vector2) == alignof(float32), "Vector2 alignment is incorrect.");

} // namespace shz