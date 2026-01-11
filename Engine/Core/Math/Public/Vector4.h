#pragma once
#include "Engine/Core/Math/Public/Constants.h"
#include <limits>

namespace shz
{
	struct Vector4 final
	{
		float32 x;
		float32 y;
		float32 z;
		float32 w;

		constexpr Vector4() : x(0.0f), y(0.0f), z(0.0f), w(0.0f) {}
		constexpr Vector4(float32 x, float32 y, float32 z, float32 w) : x(x), y(y), z(z), w(w) {}
		constexpr Vector4(const Vector4& other) = default;
		Vector4& operator=(const Vector4& other) = default;
		~Vector4() = default;

		inline float32& operator[](size_t idx)
		{
			ASSERT(idx < 4, "FVector4 index out of range.");
			return (&x)[idx];
		}
		inline const float32& operator[](size_t idx) const
		{
			ASSERT(idx < 4, "FVector4 index out of range.");
			return (&x)[idx];
		}

		// Common constants
		static inline constexpr Vector4 Zero() { return Vector4{ 0.f, 0.f, 0.f, 0.f }; }
		static inline constexpr Vector4 One() { return Vector4{ 1.f, 1.f, 1.f, 1.f }; }
		static inline constexpr Vector4 UnitX() { return Vector4{ 1.f, 0.f, 0.f, 0.f }; }
		static inline constexpr Vector4 UnitY() { return Vector4{ 0.f, 1.f, 0.f, 0.f }; }
		static inline constexpr Vector4 UnitZ() { return Vector4{ 0.f, 0.f, 1.f, 0.f }; }
		static inline constexpr Vector4 UnitW() { return Vector4{ 0.f, 0.f, 0.f, 1.f }; }

		static inline Vector4 FMaxValue() { constexpr float32 v = std::numeric_limits<float32>::max(); return Vector4{ v, v, v, v }; }
		static inline Vector4 FMinValue() { constexpr float32 v = std::numeric_limits<float32>::lowest(); return Vector4{ v, v, v, v }; }

		// Basic operations
		inline float32 Dot(const Vector4& other) const { return x * other.x + y * other.y + z * other.z + w * other.w; }
		inline float32 SqrMagnitude() const { return Dot(*this); }
		inline float32 Magnitude() const { return std::sqrt(SqrMagnitude()); }
		inline float32 Length() const { return Magnitude(); }
		inline Vector4 Normalized() const
		{
			const float32 lenSq = SqrMagnitude();
			ASSERT(lenSq > 1e-16f, "Attempted to normalize a vector with zero length.");
			return *this / std::sqrt(lenSq);
		}
		inline void Normalize(float32 epsilon = 1e-8f) { *this = Normalized(); }

		// Compound assignments
		inline Vector4& operator+=(const Vector4& other) { *this = *this + other; return *this; }
		inline Vector4& operator-=(const Vector4& other) { *this = *this - other; return *this; }
		inline Vector4& operator*=(const Vector4& other) { *this = *this * other; return *this; }
		inline Vector4& operator/=(const Vector4& other) { *this = *this / other; return *this; }
		inline Vector4& operator*=(float32 v) { *this = *this * v; return *this; }
		inline Vector4& operator/=(float32 v) { *this = *this / v; return *this; }

		// Static helpers
		static inline float32 Dot(const Vector4& a, const Vector4& b) { return a.Dot(b); }
		static inline float32 Magnitude(const Vector4& v) { return v.Magnitude(); }
		static inline float32 SqrMagnitude(const Vector4& v) { return v.SqrMagnitude(); }
		static inline float32 Length(const Vector4& v) { return v.Length(); }
		static inline Vector4 Normalize(const Vector4& v) { return v.Normalized(); }

		static inline Vector4 Abs(const Vector4& v) { return Vector4{ std::fabs(v.x), std::fabs(v.y), std::fabs(v.z), std::fabs(v.w) }; }
		static inline Vector4 Min(const Vector4& a, const Vector4& b) { return Vector4{ std::fmin(a.x, b.x), std::fmin(a.y, b.y), std::fmin(a.z, b.z), std::fmin(a.w, b.w) }; }
		static inline Vector4 Max(const Vector4& a, const Vector4& b) { return Vector4{ std::fmax(a.x, b.x), std::fmax(a.y, b.y), std::fmax(a.z, b.z), std::fmax(a.w, b.w) }; }
		static inline float32 MinComponent(const Vector4& v) { return std::fmin(std::fmin(v.x, v.y), std::fmin(v.z, v.w)); }
		static inline float32 MaxComponent(const Vector4& v) { return std::fmax(std::fmax(v.x, v.y), std::fmax(v.z, v.w)); }

		static inline Vector4 Clamp(const Vector4& value, float32 min, float32 max)
		{
			return Clamp(value, Vector4{ min, min, min, min }, Vector4{ max, max, max, max });
		}
		static inline Vector4 Clamp(const Vector4& value, const Vector4& min, const Vector4& max)
		{
			return Vector4{
				std::fmax(min.x, std::fmin(value.x, max.x)),
				std::fmax(min.y, std::fmin(value.y, max.y)),
				std::fmax(min.z, std::fmin(value.z, max.z)),
				std::fmax(min.w, std::fmin(value.w, max.w))
			};
		}

		static inline Vector4 Lerp(const Vector4& a, const Vector4& b, float32 t)
		{
			return Vector4{
				a.x + (b.x - a.x) * t,
				a.y + (b.y - a.y) * t,
				a.z + (b.z - a.z) * t,
				a.w + (b.w - a.w) * t
			};
		}

		static inline Vector4 SmoothStep(const Vector4& a, const Vector4& b, float32 t)
		{
			t = std::fmax(0.f, std::fmin(t, 1.f)); // keep clamped version
			const float32 s = t * t * (3.f - 2.f * t);
			return Vector4{
				a.x + (b.x - a.x) * s,
				a.y + (b.y - a.y) * s,
				a.z + (b.z - a.z) * s,
				a.w + (b.w - a.w) * s
			};
		}

		// Operators
		inline Vector4 operator-() const { return Vector4{ -x, -y, -z, -w }; }

		inline Vector4 operator+(const Vector4& rhs) const { return Vector4{ x + rhs.x, y + rhs.y, z + rhs.z, w + rhs.w }; }
		inline Vector4 operator-(const Vector4& rhs) const { return Vector4{ x - rhs.x, y - rhs.y, z - rhs.z, w - rhs.w }; }
		inline Vector4 operator*(const Vector4& rhs) const { return Vector4{ x * rhs.x, y * rhs.y, z * rhs.z, w * rhs.w }; }
		inline Vector4 operator/(const Vector4& rhs) const { return Vector4{ x / rhs.x, y / rhs.y, z / rhs.z, w / rhs.w }; }

		inline Vector4 operator+(float32 s) const { return Vector4{ x + s, y + s, z + s, w + s }; }
		inline Vector4 operator-(float32 s) const { return Vector4{ x - s, y - s, z - s, w - s }; }
		inline Vector4 operator*(float32 s) const { return Vector4{ x * s, y * s, z * s, w * s }; }
		inline Vector4 operator/(float32 s) const { return Vector4{ x / s, y / s, z / s, w / s }; }

		inline bool operator==(const Vector4& rhs) const { return (x == rhs.x) && (y == rhs.y) && (z == rhs.z) && (w == rhs.w); }
		inline bool operator!=(const Vector4& rhs) const { return !(*this == rhs); }

		friend inline Vector4 operator*(float32 s, const Vector4& v) { return Vector4{ s * v.x, s * v.y, s * v.z, s * v.w }; }
	};
	static_assert(sizeof(Vector4) == 16, "Vector4 size mismatch");
	static_assert(alignof(Vector4) == alignof(float32), "Vector4 alignment is incorrect.");

} // namespace shz