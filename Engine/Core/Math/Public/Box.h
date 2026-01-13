#pragma once
#include "Engine/Core/Math/Public/Vector3.h"

namespace shz
{
	struct Box
	{
		Vector3 Min;
		Vector3 Max;

		constexpr Box() : Min(Vector3::FMaxValue()), Max(Vector3::FMinValue()) {};
		constexpr Box(const Vector3& min, const Vector3& max) : Min(min), Max(max) {}

		inline Vector3 Center() const { return (Min + Max) * 0.5f; }
		inline Vector3 Size() const { return Max - Min; }
		inline Vector3 Extents() const { return Size() * 0.5f; }
		inline float Volume() const { Vector3 size = Size(); return size.x * size.y * size.z; }

		inline void Encapsulate(const Vector3& point)
		{
			Min = Vector3::Min(Min, point);
			Max = Vector3::Max(Max, point);
		}

		inline void Encapsulate(const Box& other)
		{
			Encapsulate(other.Min);
			Encapsulate(other.Max);
		}

		inline bool Contains(const Vector3& point) const
		{
			return (point.x >= Min.x && point.x <= Max.x) &&
				(point.y >= Min.y && point.y <= Max.y) &&
				(point.z >= Min.z && point.z <= Max.z);
		}

		inline bool Overlaps(const Box& other) const
		{
			return (Min.x <= other.Max.x && Max.x >= other.Min.x) &&
				(Min.y <= other.Max.y && Max.y >= other.Min.y) &&
				(Min.z <= other.Max.z && Max.z >= other.Min.z);
		}

		static bool Overlaps(const Box& a, const Box& b)
		{
			return a.Overlaps(b);
		}
	};
	static_assert(sizeof(Box) == 24, "Wrong size of Bounds struct");
} // namespace shz