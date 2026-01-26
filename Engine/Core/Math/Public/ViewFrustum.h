#pragma once
#include "Primitives/BasicTypes.h"
#include "Primitives/FlagEnum.h"

#include "Engine/Core/Math/Public/Vector3.h"
#include "Engine/Core/Math/Public/Vector4.h"
#include "Engine/Core/Math/Public/Matrix4x4.h"
#include "Engine/Core/Math/Public/Plane.h"
#include "Engine/Core/Math/Public/OrientedBox.h"

namespace shz
{
	struct ViewFrustum
	{
		enum PLANE_IDX : uint32
		{
			LEFT_PLANE_IDX = 0,
			RIGHT_PLANE_IDX = 1,
			BOTTOM_PLANE_IDX = 2,
			TOP_PLANE_IDX = 3,
			NEAR_PLANE_IDX = 4,
			FAR_PLANE_IDX = 5,
			NUM_PLANES = 6
		};

		Plane LeftPlane = {};
		Plane RightPlane = {};
		Plane BottomPlane = {};
		Plane TopPlane = {};
		Plane NearPlane = {};
		Plane FarPlane = {};

		const Plane& GetPlane(PLANE_IDX idx) const
		{
			ASSERT_EXPR(idx < NUM_PLANES);
			const Plane* planes = reinterpret_cast<const Plane*>(this);
			return planes[static_cast<size_t>(idx)];
		}

		Plane& GetPlane(PLANE_IDX idx)
		{
			ASSERT_EXPR(idx < NUM_PLANES);
			Plane* planes = reinterpret_cast<Plane*>(this);
			return planes[static_cast<size_t>(idx)];
		}
	};

	struct ViewFrustumExt : public ViewFrustum
	{
		Vector3 FrustumCorners[8] = {};
	};

	inline bool operator==(const ViewFrustum& a, const ViewFrustum& b) noexcept
	{
		return a.LeftPlane == b.LeftPlane &&
			a.RightPlane == b.RightPlane &&
			a.BottomPlane == b.BottomPlane &&
			a.TopPlane == b.TopPlane &&
			a.NearPlane == b.NearPlane &&
			a.FarPlane == b.FarPlane;
	}

	inline bool operator!=(const ViewFrustum& a, const ViewFrustum& b) noexcept
	{
		return !(a == b);
	}

	inline bool operator==(const ViewFrustumExt& a, const ViewFrustumExt& b) noexcept
	{
		if (!(static_cast<const ViewFrustum&>(a) == static_cast<const ViewFrustum&>(b)))
			return false;

		for (size_t i = 0; i < _countof(a.FrustumCorners); ++i)
		{
			if (a.FrustumCorners[i] != b.FrustumCorners[i])
				return false;
		}
		return true;
	}

	inline bool operator!=(const ViewFrustumExt& a, const ViewFrustumExt& b) noexcept
	{
		return !(a == b);
	}

	// Returned normals are NOT normalized.
	inline void ExtractViewFrustumPlanesFromMatrix(const Matrix4x4& matrix, ViewFrustum& frustum)
	{
		// Left
		frustum.LeftPlane.Normal.x = matrix._m03 + matrix._m00;
		frustum.LeftPlane.Normal.y = matrix._m13 + matrix._m10;
		frustum.LeftPlane.Normal.z = matrix._m23 + matrix._m20;
		frustum.LeftPlane.Distance = matrix._m33 + matrix._m30;

		// Right
		frustum.RightPlane.Normal.x = matrix._m03 - matrix._m00;
		frustum.RightPlane.Normal.y = matrix._m13 - matrix._m10;
		frustum.RightPlane.Normal.z = matrix._m23 - matrix._m20;
		frustum.RightPlane.Distance = matrix._m33 - matrix._m30;

		// Top
		frustum.TopPlane.Normal.x = matrix._m03 - matrix._m01;
		frustum.TopPlane.Normal.y = matrix._m13 - matrix._m11;
		frustum.TopPlane.Normal.z = matrix._m23 - matrix._m21;
		frustum.TopPlane.Distance = matrix._m33 - matrix._m31;

		// Bottom
		frustum.BottomPlane.Normal.x = matrix._m03 + matrix._m01;
		frustum.BottomPlane.Normal.y = matrix._m13 + matrix._m11;
		frustum.BottomPlane.Normal.z = matrix._m23 + matrix._m21;
		frustum.BottomPlane.Distance = matrix._m33 + matrix._m31;

		// Near
		frustum.NearPlane.Normal.x = matrix._m02;
		frustum.NearPlane.Normal.y = matrix._m12;
		frustum.NearPlane.Normal.z = matrix._m22;
		frustum.NearPlane.Distance = matrix._m32;

		// Far
		frustum.FarPlane.Normal.x = matrix._m03 - matrix._m02;
		frustum.FarPlane.Normal.y = matrix._m13 - matrix._m12;
		frustum.FarPlane.Normal.z = matrix._m23 - matrix._m22;
		frustum.FarPlane.Distance = matrix._m33 - matrix._m32;
	}

	inline void ExtractViewFrustumPlanesFromMatrix(const Matrix4x4& matrix, ViewFrustumExt& frustumExt)
	{
		ExtractViewFrustumPlanesFromMatrix(matrix, static_cast<ViewFrustum&>(frustumExt));

		const Matrix4x4 inv = matrix.Inversed();
		const float nearZ = 0.f;

		for (uint32 i = 0; i < 8; ++i)
		{
			const Vector3 ndc =
			{
				(i & 0x01u) ? +1.f : -1.f,
				(i & 0x02u) ? +1.f : -1.f,
				(i & 0x04u) ? +1.f : nearZ,
			};

			// NOTE: assumes float4x4::MulVector4(float4) exists like your snippet.
			const Vector4 p = Vector4(ndc.x, ndc.y, ndc.z, 1.f);
			const Vector4 w = inv.MulVector4(p);

			frustumExt.FrustumCorners[i] = Vector3(w.x / w.w, w.y / w.w, w.z / w.w);
		}
	}
	enum class BoxVisibility : uint8
	{
		Invisible,
		Intersecting,
		FullyVisible
	};

	// Flags must match ViewFrustum plane order:
	// Left, Right, Bottom, Top, Near, Far
	enum FRUSTUM_PLANE_FLAGS : uint32
	{
		FRUSTUM_PLANE_FLAG_NONE = 0x00,
		FRUSTUM_PLANE_FLAG_LEFT_PLANE = 1u << ViewFrustum::LEFT_PLANE_IDX,
		FRUSTUM_PLANE_FLAG_RIGHT_PLANE = 1u << ViewFrustum::RIGHT_PLANE_IDX,
		FRUSTUM_PLANE_FLAG_BOTTOM_PLANE = 1u << ViewFrustum::BOTTOM_PLANE_IDX,
		FRUSTUM_PLANE_FLAG_TOP_PLANE = 1u << ViewFrustum::TOP_PLANE_IDX,
		FRUSTUM_PLANE_FLAG_NEAR_PLANE = 1u << ViewFrustum::NEAR_PLANE_IDX,
		FRUSTUM_PLANE_FLAG_FAR_PLANE = 1u << ViewFrustum::FAR_PLANE_IDX,

		FRUSTUM_PLANE_FLAG_FULL_FRUSTUM =
		FRUSTUM_PLANE_FLAG_LEFT_PLANE |
		FRUSTUM_PLANE_FLAG_RIGHT_PLANE |
		FRUSTUM_PLANE_FLAG_BOTTOM_PLANE |
		FRUSTUM_PLANE_FLAG_TOP_PLANE |
		FRUSTUM_PLANE_FLAG_NEAR_PLANE |
		FRUSTUM_PLANE_FLAG_FAR_PLANE,

		FRUSTUM_PLANE_FLAG_OPEN_NEAR =
		FRUSTUM_PLANE_FLAG_LEFT_PLANE |
		FRUSTUM_PLANE_FLAG_RIGHT_PLANE |
		FRUSTUM_PLANE_FLAG_BOTTOM_PLANE |
		FRUSTUM_PLANE_FLAG_TOP_PLANE |
		FRUSTUM_PLANE_FLAG_FAR_PLANE
	};
	DEFINE_FLAG_ENUM_OPERATORS(FRUSTUM_PLANE_FLAGS);

	// -------------------------------------------------------------------------
	// Plane vs OBB
	// -------------------------------------------------------------------------
	inline BoxVisibility GetBoxVisibilityAgainstPlane(
		const Plane& plane,
		const OrientedBox& box)
	{
		const float dist = Vector3::Dot(box.Center, plane.Normal) + plane.Distance;

		const float projHalf =
			std::abs(Vector3::Dot(box.Axes[0], plane.Normal)) * box.HalfExtents[0] +
			std::abs(Vector3::Dot(box.Axes[1], plane.Normal)) * box.HalfExtents[1] +
			std::abs(Vector3::Dot(box.Axes[2], plane.Normal)) * box.HalfExtents[2];

		if (dist < -projHalf)
		{
			return BoxVisibility::Invisible;
		}

		if (dist > projHalf)
		{
			return BoxVisibility::FullyVisible;
		}

		return BoxVisibility::Intersecting;
	}

	// -------------------------------------------------------------------------
	// Frustum vs OBB
	// -------------------------------------------------------------------------
	inline BoxVisibility GetBoxVisibility(const ViewFrustum& frustum, const OrientedBox& box, FRUSTUM_PLANE_FLAGS planeFlags = FRUSTUM_PLANE_FLAG_FULL_FRUSTUM)
	{
		int insidePlanes = 0;
		int totalPlanes = 0;

		for (uint32 planeIdx = 0; planeIdx < ViewFrustum::NUM_PLANES; ++planeIdx)
		{
			if ((planeFlags & (1u << planeIdx)) == 0)
			{
				continue;
			}

			const Plane& plane = frustum.GetPlane(static_cast<ViewFrustum::PLANE_IDX>(planeIdx));
			const BoxVisibility v = GetBoxVisibilityAgainstPlane(plane, box);

			if (v == BoxVisibility::Invisible)
			{
				return BoxVisibility::Invisible;
			}

			if (v == BoxVisibility::FullyVisible)
			{
				++insidePlanes;
			}

			++totalPlanes;
		}

		return (insidePlanes == totalPlanes) ? BoxVisibility::FullyVisible : BoxVisibility::Intersecting;
	}

	inline BoxVisibility GetBoxVisibility(
		const ViewFrustum& frustum,
		const Box& localAabb,
		const Matrix4x4& world,
		FRUSTUM_PLANE_FLAGS  planeFlags = FRUSTUM_PLANE_FLAG_FULL_FRUSTUM)
	{
		const OrientedBox obb = BuildOBBFromAABBAndMatrix(localAabb, world);
		return GetBoxVisibility(frustum, obb, planeFlags);
	}

	inline bool IntersectsFrustum(
		const ViewFrustum& frustum,
		const Box& localAabb,
		const Matrix4x4& world,
		FRUSTUM_PLANE_FLAGS  planeFlags = FRUSTUM_PLANE_FLAG_FULL_FRUSTUM)
	{
		return GetBoxVisibility(frustum, localAabb, world, planeFlags) != BoxVisibility::Invisible;
	}
} // namespace shz
