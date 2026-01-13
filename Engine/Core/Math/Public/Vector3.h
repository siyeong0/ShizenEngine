#pragma once
#include "Engine/Core/Math/Public/Constants.h"
#include <limits>

namespace shz
{
	struct Vector3 final
	{
		float32 x;
		float32 y;
		float32 z;

		constexpr Vector3() : x(0.0f), y(0.0f), z(0.0f) {}
		constexpr Vector3(float32 x, float32 y, float32 z) : x(x), y(y), z(z) {}
		constexpr Vector3(const Vector3& other) = default;
		Vector3& operator=(const Vector3& other) = default;
		~Vector3() = default;

		inline float32& operator[](size_t idx)
		{
			ASSERT(idx < 3, "Vector3 index out of range.");
			return (&x)[idx];
		}
		inline const float32& operator[](size_t idx) const
		{
			ASSERT(idx < 3, "FVector3 index out of range.");
			return (&x)[idx];
		}

		// Common constants
		static inline constexpr Vector3 Zero() { return Vector3{ 0.f, 0.f, 0.f }; }
		static inline constexpr Vector3 One() { return Vector3{ 1.f, 1.f, 1.f }; }
		static inline constexpr Vector3 UnitX() { return Vector3{ 1.f, 0.f, 0.f }; }
		static inline constexpr Vector3 UnitY() { return Vector3{ 0.f, 1.f, 0.f }; }
		static inline constexpr Vector3 UnitZ() { return Vector3{ 0.f, 0.f, 1.f }; }

		static inline constexpr Vector3 FMaxValue() { constexpr float32 v = std::numeric_limits<float32>::max(); return Vector3{ v, v, v }; }
		static inline constexpr Vector3 FMinValue() { constexpr float32 v = std::numeric_limits<float32>::lowest(); return Vector3{ v, v, v }; }

		// Direction aliases (same as your previous version)
		static inline constexpr Vector3 Up() { return Vector3{ 0.f,  1.f,  0.f }; }
		static inline constexpr Vector3 Down() { return Vector3{ 0.f, -1.f,  0.f }; }
		static inline constexpr Vector3 Right() { return Vector3{ 1.f,  0.f,  0.f }; }
		static inline constexpr Vector3 Left() { return Vector3{ -1.f, 0.f,  0.f }; }
		static inline constexpr Vector3 Forward() { return Vector3{ 0.f,  0.f,  1.f }; }
		static inline constexpr Vector3 Backward() { return Vector3{ 0.f,  0.f, -1.f }; }

		// Basic operations
		inline float32 Dot(const Vector3& other) const { return x * other.x + y * other.y + z * other.z; }
		inline Vector3 Cross(const Vector3& other) const
		{
			return Vector3{
				y * other.z - z * other.y,
				z * other.x - x * other.z,
				x * other.y - y * other.x
			};
		}

		inline float32 SqrMagnitude() const { return Dot(*this); }
		inline float32 Magnitude() const { return std::sqrt(SqrMagnitude()); }
		inline float32 Length() const { return Magnitude(); }
		inline Vector3 Normalized() const
		{
			const float32 lenSq = SqrMagnitude();
			ASSERT(lenSq > 1e-16f, "Attempted to normalize a vector with zero length.");
			return *this / std::sqrt(lenSq);
		}
		inline void Normalize() { *this = Normalized(); }

		// Compound assignments
		inline Vector3& operator+=(const Vector3& other) { *this = *this + other; return *this; }
		inline Vector3& operator-=(const Vector3& other) { *this = *this - other; return *this; }
		inline Vector3& operator*=(const Vector3& other) { *this = *this * other; return *this; }
		inline Vector3& operator/=(const Vector3& other) { *this = *this / other; return *this; }
		inline Vector3& operator*=(float32 v) { *this = *this * v; return *this; }
		inline Vector3& operator/=(float32 v) { *this = *this / v; return *this; }

		// Comparison operator for sorting
		inline bool operator<(const Vector3& r) const noexcept
		{
			if (x != r.x) return x < r.x;
			if (y != r.y) return y < r.y;
			return z < r.z;
		}

		// Static helpers
		static inline float32 Dot(const Vector3& a, const Vector3& b) { return a.Dot(b); }
		static inline Vector3 Cross(const Vector3& a, const Vector3& b) { return a.Cross(b); }

		static inline float32 Magnitude(const Vector3& v) { return v.Magnitude(); }
		static inline float32 SqrMagnitude(const Vector3& v) { return v.SqrMagnitude(); }
		static inline float32 Length(const Vector3& v) { return v.Length(); }

		static inline Vector3 Normalize(const Vector3& v) { return v.Normalized(); }

		static inline Vector3 Abs(const Vector3& v) { return Vector3{ std::fabs(v.x), std::fabs(v.y), std::fabs(v.z) }; }
		static inline Vector3 Min(const Vector3& a, const Vector3& b) { return Vector3{ std::fmin(a.x, b.x), std::fmin(a.y, b.y), std::fmin(a.z, b.z) }; }
		static inline Vector3 Max(const Vector3& a, const Vector3& b) { return Vector3{ std::fmax(a.x, b.x), std::fmax(a.y, b.y), std::fmax(a.z, b.z) }; }
		static inline float32 MinComponent(const Vector3& v) { return std::fmin(std::fmin(v.x, v.y), v.z); }
		static inline float32 MaxComponent(const Vector3& v) { return std::fmax(std::fmax(v.x, v.y), v.z); }

		static inline Vector3 Clamp(const Vector3& value, float32 min, float32 max)
		{
			return Clamp(value, Vector3{ min, min, min }, Vector3{ max, max, max });
		}
		static inline Vector3 Clamp(const Vector3& value, const Vector3& min, const Vector3& max)
		{
			return Vector3{
				std::fmax(min.x, std::fmin(value.x, max.x)),
				std::fmax(min.y, std::fmin(value.y, max.y)),
				std::fmax(min.z, std::fmin(value.z, max.z))
			};
		}

		static inline Vector3 Lerp(const Vector3& a, const Vector3& b, float32 t)
		{
			return Vector3{
				a.x + (b.x - a.x) * t,
				a.y + (b.y - a.y) * t,
				a.z + (b.z - a.z) * t
			};
		}

		static inline Vector3 SmoothStep(const Vector3& a, const Vector3& b, float32 t)
		{
			t = std::fmax(0.f, std::fmin(t, 1.f)); // keep clamped version
			const float32 s = t * t * (3.f - 2.f * t);
			return Vector3{
				a.x + (b.x - a.x) * s,
				a.y + (b.y - a.y) * s,
				a.z + (b.z - a.z) * s
			};
		}

		// Operators
		inline Vector3 operator-() const { return Vector3{ -x, -y, -z }; }

		inline Vector3 operator+(const Vector3& rhs) const { return Vector3{ x + rhs.x, y + rhs.y, z + rhs.z }; }
		inline Vector3 operator-(const Vector3& rhs) const { return Vector3{ x - rhs.x, y - rhs.y, z - rhs.z }; }
		inline Vector3 operator*(const Vector3& rhs) const { return Vector3{ x * rhs.x, y * rhs.y, z * rhs.z }; }
		inline Vector3 operator/(const Vector3& rhs) const { return Vector3{ x / rhs.x, y / rhs.y, z / rhs.z }; }

		inline Vector3 operator+(float32 s) const { return Vector3{ x + s, y + s, z + s }; }
		inline Vector3 operator-(float32 s) const { return Vector3{ x - s, y - s, z - s }; }
		inline Vector3 operator*(float32 s) const { return Vector3{ x * s, y * s, z * s }; }
		inline Vector3 operator/(float32 s) const { return Vector3{ x / s, y / s, z / s }; }

		inline bool operator==(const Vector3& rhs) const { return (x == rhs.x) && (y == rhs.y) && (z == rhs.z); }
		inline bool operator!=(const Vector3& rhs) const { return !(*this == rhs); }

		friend inline Vector3 operator*(float32 s, const Vector3& v) { return Vector3{ s * v.x, s * v.y, s * v.z }; }
	};
	static_assert(sizeof(Vector3) == 12, "Vector3 size mismatch");
	static_assert(alignof(Vector3) == alignof(float32), "Vector3 alignment is incorrect.");

} // namespace shz
