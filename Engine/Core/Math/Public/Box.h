#pragma once
#include "Engine/Core/Math/Public/Vector3.h"

namespace shz
{
	class Box
	{
	public:
		constexpr Box() : m_Min(Vector3::FMaxValue()), m_Max(Vector3::FMinValue()) {};
		constexpr Box(const Vector3& min, const Vector3& max) : m_Min(min), m_Max(max) {}

		inline const Vector3& Min() const { return m_Min; }
		inline const Vector3& Max() const { return m_Max; }
		inline Vector3 Center() const { return (m_Min + m_Max) * 0.5f; }
		inline Vector3 Size() const { return m_Max - m_Min; }
		inline Vector3 Extents() const { return Size() * 0.5f; }
		inline float Volume() const { Vector3 size = Size(); return size.x * size.y * size.z; }

		inline void Encapsulate(const Vector3& point)
		{
			m_Min = Vector3::Min(m_Min, point);
			m_Max = Vector3::Max(m_Max, point);
		}

		inline void Encapsulate(const Box& other)
		{
			Encapsulate(other.Min());
			Encapsulate(other.Max());
		}

		inline bool Contains(const Vector3& point) const
		{
			return (point.x >= m_Min.x && point.x <= m_Max.x) &&
				(point.y >= m_Min.y && point.y <= m_Max.y) &&
				(point.z >= m_Min.z && point.z <= m_Max.z);
		}

		inline bool Overlaps(const Box& other) const
		{
			return (m_Min.x <= other.m_Max.x && m_Max.x >= other.m_Min.x) &&
				(m_Min.y <= other.m_Max.y && m_Max.y >= other.m_Min.y) &&
				(m_Min.z <= other.m_Max.z && m_Max.z >= other.m_Min.z);
		}

		static bool Overlaps(const Box& a, const Box& b)
		{
			return a.Overlaps(b);
		}
	private:
		Vector3 m_Min;
		Vector3 m_Max;
	};
	static_assert(sizeof(Box) == 24, "Wrong size of Bounds struct");
} // namespace shz