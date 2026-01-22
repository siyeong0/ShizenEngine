#pragma once
#include "Primitives/BasicTypes.h"
#include "Engine/Core/Math/Public/Vector3.h"
#include "Engine/Core/Math/Public/Vector4.h"
#include "Engine/Core/Math/Public/Matrix4x4.h"
#include "Engine/Core/Math/Public/Box.h"

namespace shz
{
	struct OrientedBox final
	{
		Vector3 Center = Vector3(0, 0, 0);

		// Normalized axes in world space
		Vector3 Axes[3] =
		{
			Vector3(1, 0, 0),
			Vector3(0, 1, 0),
			Vector3(0, 0, 1)
		};

		// Half extents along each axis (world units)
		float  HalfExtents[3] = { 0.f, 0.f, 0.f };
	};

	// Builds OBB from local AABB and world matrix (affine).
	// - Axis = normalized world basis
	// - HalfExtent = localHalfExtent * |basis|
	inline OrientedBox BuildOBBFromAABBAndMatrix(const Box& localAabb, const Matrix4x4& world)
	{
		OrientedBox obb = {};

		const Vector3 localCenter = (localAabb.Min + localAabb.Max) * 0.5f;
		const Vector3 localHalf = (localAabb.Max - localAabb.Min) * 0.5f;

		// World center
		{
			const Vector4 p(localCenter.x, localCenter.y, localCenter.z, 1.f);
			const Vector4 wp = world.MulVector4(p);
			obb.Center = Vector3(wp.x, wp.y, wp.z);
		}

		// Row-major basis vectors (same as your BoundBox::Transform usage)
		Vector3 axisX = { world._m00, world._m01, world._m02 };
		Vector3 axisY = { world._m10, world._m11, world._m12 };
		Vector3 axisZ = { world._m20, world._m21, world._m22 };

		const float lenX = Vector3::Length(axisX);
		const float lenY = Vector3::Length(axisY);
		const float lenZ = Vector3::Length(axisZ);

		if (lenX > 0.f) axisX /= lenX;
		if (lenY > 0.f) axisY /= lenY;
		if (lenZ > 0.f) axisZ /= lenZ;

		obb.Axes[0] = axisX;
		obb.Axes[1] = axisY;
		obb.Axes[2] = axisZ;

		obb.HalfExtents[0] = localHalf.x * lenX;
		obb.HalfExtents[1] = localHalf.y * lenY;
		obb.HalfExtents[2] = localHalf.z * lenZ;

		return obb;
	}

} // namespace shz
