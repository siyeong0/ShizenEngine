#pragma once
#include "Engine/Core/Math/Public/Vector3.h"

namespace shz
{
	class Sphere
	{
	public:
		constexpr Sphere() : m_Center(Vector3::One()), m_Radius(0.0f) {};
		constexpr Sphere(const Vector3& center, float radius) : m_Center(center), m_Radius(radius) {}

		inline Vector3 Center() const { return m_Center; }
		inline float Radius() const { return m_Radius; }
		inline float Volume() const { return (4.0f / 3.0f) * PI * m_Radius * m_Radius * m_Radius; }

		inline void Encapsulate(const Vector3& point)
		{
			Vector3 toPoint = point - m_Center;
			float distSq = toPoint.SqrMagnitude();
			if (distSq > m_Radius * m_Radius)
			{
				float dist = std::sqrt(distSq);
				float newRadius = (m_Radius + dist) * 0.5f;
				Vector3 direction = toPoint / dist; // Normalize the direction
				m_Center += direction * (newRadius - m_Radius); // Move center towards the point
				m_Radius = newRadius;
			}
			// If the point is inside the sphere, do nothing
		}

		inline bool Contains(const Vector3& point) const
		{
			Vector3 toPoint = point - m_Center;
			return toPoint.SqrMagnitude() <= m_Radius * m_Radius;
		}

		inline bool Overlaps(const Sphere& other) const
		{
			Vector3 toOther = other.m_Center - m_Center;
			float radiusSum = m_Radius + other.m_Radius;
			return toOther.SqrMagnitude() <= radiusSum * radiusSum;
		}

		static bool Overlaps(const Sphere& a, const Sphere& b)
		{
			return a.Overlaps(b);
		}
	private:
		Vector3 m_Center;
		float m_Radius;
	};
	static_assert(sizeof(Sphere) == 16, "Wrong size of Sphere struct");
} // namespace shz