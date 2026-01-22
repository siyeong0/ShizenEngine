#pragma once
#include "Primitives/BasicTypes.h"
#include "Engine/Core/Math/Public/Vector3.h"
#include "Engine/Core/Math/Public/Vector4.h"

namespace shz
{
	struct Plane final
	{
		// Plane normal.
		Vector3 Normal = Vector3(0, 0, 0);

		// Plane distance (same unit as Normal).
		float Distance = 0.f;

		constexpr Plane() noexcept = default;

		constexpr Plane(const Vector3& normal, float distance) noexcept
			: Normal(normal)
			, Distance(distance)
		{
		}

		// Treat as float4 (Normal.xyz, Distance)
		operator Vector4& ()
		{
			return *reinterpret_cast<Vector4*>(this);
		}

		operator const Vector4& () const
		{
			return *reinterpret_cast<const Vector4*>(this);
		}
	};

	inline bool operator==(const Plane& a, const Plane& b) noexcept
	{
		return a.Normal == b.Normal &&
			a.Distance == b.Distance;
	}

	inline bool operator!=(const Plane& a, const Plane& b) noexcept
	{
		return !(a == b);
	}
} // namespace shz
