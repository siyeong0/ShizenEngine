/*
 *  Copyright 2019-2025 Diligent Graphics LLC
 *  Copyright 2015-2019 Egor Yusov
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *  In no event and under no legal theory, whether in tort (including negligence),
 *  contract, or otherwise, unless required by applicable law (such as deliberate
 *  and grossly negligent acts) or agreed to in writing, shall any Contributor be
 *  liable for any damages, including any direct, indirect, special, incidental,
 *  or consequential damages of any character arising as a result of this License or
 *  out of the use or inability to use the software (including but not limited to damages
 *  for loss of goodwill, work stoppage, computer failure or malfunction, or any and
 *  all other commercial damages or losses), even if such Contributor has been advised
 *  of the possibility of such damages.
 */

#pragma once

 // \file
 // Additional math functions and structures.

#include <float.h>
#include <vector>
#include <type_traits>

#include "Platforms/Common/PlatformDefinitions.h"
#include "Primitives/FlagEnum.h"

#include "Engine/Core/Math/Math.h"

namespace shz
{

	// A plane in 3D space described by the plane equation:
	//
	//     dot(Normal, Point) + Distance = 0
	struct Plane3D
	{
		// Plane normal.
		//
		// \note  The normal does not have to be normalized as long
		//        as it is measured in the same units as Distance.
		float3 Normal;

		// Distance from the plane to the coordinate system origin along the normal direction:
		//
		//     dot(Normal, Point) = -Distance
		//
		//
		//     O         |   N
		//     *<--------|==>
		//               |
		//
		// \note   The distance is measured in the same units as the normal vector.
		float Distance = 0;

		operator float4& ()
		{
			return *reinterpret_cast<float4*>(this);
		}
		operator const float4& () const
		{
			return *reinterpret_cast<const float4*>(this);
		}
	};

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

		Plane3D LeftPlane;
		Plane3D RightPlane;
		Plane3D BottomPlane;
		Plane3D TopPlane;
		Plane3D NearPlane;
		Plane3D FarPlane;

		const Plane3D& GetPlane(PLANE_IDX Idx) const
		{
			VERIFY_EXPR(Idx < NUM_PLANES);
			const Plane3D* Planes = reinterpret_cast<const Plane3D*>(this);
			return Planes[static_cast<size_t>(Idx)];
		}

		Plane3D& GetPlane(PLANE_IDX Idx)
		{
			VERIFY_EXPR(Idx < NUM_PLANES);
			Plane3D* Planes = reinterpret_cast<Plane3D*>(this);
			return Planes[static_cast<size_t>(Idx)];
		}
	};

	struct ViewFrustumExt : public ViewFrustum
	{
		float3 FrustumCorners[8];
	};

	// For OpenGL, matrix is still considered row-major. The only difference is that
	// near clip plane is at -1, not 0.
	//
	// Note that returned plane normal vectors are not normalized, which does not make a difference
	// when testing a point against the plane:
	//
	//      IsPointInsidePlane = dot(Plane.Normal, Point) < Plane.Distance
	//
	// However, to use the planes with other distances (e.g. for testing a sphere against the plane),
	// the normal vectors must be normalized and the distances scaled accordingly.
	inline void ExtractViewFrustumPlanesFromMatrix(const float4x4& Matrix, ViewFrustum& Frustum, bool bIsOpenGL)
	{
		// Left clipping plane
		Frustum.LeftPlane.Normal.x = Matrix._m03 + Matrix._m00;
		Frustum.LeftPlane.Normal.y = Matrix._m13 + Matrix._m10;
		Frustum.LeftPlane.Normal.z = Matrix._m23 + Matrix._m20;
		Frustum.LeftPlane.Distance = Matrix._m33 + Matrix._m30;

		// Right clipping plane
		Frustum.RightPlane.Normal.x = Matrix._m03 - Matrix._m00;
		Frustum.RightPlane.Normal.y = Matrix._m13 - Matrix._m10;
		Frustum.RightPlane.Normal.z = Matrix._m23 - Matrix._m20;
		Frustum.RightPlane.Distance = Matrix._m33 - Matrix._m30;

		// Top clipping plane
		Frustum.TopPlane.Normal.x = Matrix._m03 - Matrix._m01;
		Frustum.TopPlane.Normal.y = Matrix._m13 - Matrix._m11;
		Frustum.TopPlane.Normal.z = Matrix._m23 - Matrix._m21;
		Frustum.TopPlane.Distance = Matrix._m33 - Matrix._m31;

		// Bottom clipping plane
		Frustum.BottomPlane.Normal.x = Matrix._m03 + Matrix._m01;
		Frustum.BottomPlane.Normal.y = Matrix._m13 + Matrix._m11;
		Frustum.BottomPlane.Normal.z = Matrix._m23 + Matrix._m21;
		Frustum.BottomPlane.Distance = Matrix._m33 + Matrix._m31;

		// Near clipping plane
		if (bIsOpenGL)
		{
			// -w <= z <= w
			Frustum.NearPlane.Normal.x = Matrix._m03 + Matrix._m02;
			Frustum.NearPlane.Normal.y = Matrix._m13 + Matrix._m12;
			Frustum.NearPlane.Normal.z = Matrix._m23 + Matrix._m22;
			Frustum.NearPlane.Distance = Matrix._m33 + Matrix._m32;
		}
		else
		{
			// 0 <= z <= w (D3D / Vulkan)
			Frustum.NearPlane.Normal.x = Matrix._m02;
			Frustum.NearPlane.Normal.y = Matrix._m12;
			Frustum.NearPlane.Normal.z = Matrix._m22;
			Frustum.NearPlane.Distance = Matrix._m32;
		}

		// Far clipping plane
		Frustum.FarPlane.Normal.x = Matrix._m03 - Matrix._m02;
		Frustum.FarPlane.Normal.y = Matrix._m13 - Matrix._m12;
		Frustum.FarPlane.Normal.z = Matrix._m23 - Matrix._m22;
		Frustum.FarPlane.Distance = Matrix._m33 - Matrix._m32;
	}


	inline void ExtractViewFrustumPlanesFromMatrix(const float4x4& Matrix, ViewFrustumExt& FrustumExt, bool bIsOpenGL)
	{
		ExtractViewFrustumPlanesFromMatrix(Matrix, static_cast<ViewFrustum&>(FrustumExt), bIsOpenGL);

		// Compute frustum corners
		float4x4 InvMatrix = Matrix.Inversed();

		float NearClipZ = bIsOpenGL ? -1.f : 0.f;
		for (uint32 i = 0; i < 8; ++i)
		{
			const float3 ProjSpaceCorner{
				(i & 0x01u) ? +1.f : -1.f,
				(i & 0x02u) ? +1.f : -1.f,
				(i & 0x04u) ? +1.f : NearClipZ,
			};
			Vector4 corner = InvMatrix.MulVector4({ ProjSpaceCorner.x, ProjSpaceCorner.y, ProjSpaceCorner.z, 1.0f });
			FrustumExt.FrustumCorners[i] = { corner.x / corner.w, corner.y / corner.w, corner.z / corner.w };
		}
	}

	struct BoundBox
	{
		float3 Min;
		float3 Max;

		// Computes new bounding box by applying transform matrix m to the box
		BoundBox Transform(const float4x4& m) const
		{
			BoundBox NewBB;
			NewBB.Min = { m._m30, m._m31, m._m32 };
			NewBB.Max = NewBB.Min;
			float3 v0, v1;

			float3 right = { m._m00, m._m01, m._m02 };

			v0 = right * Min.x;
			v1 = right * Max.x;
			NewBB.Min += (std::min)(v0, v1);
			NewBB.Max += (std::max)(v0, v1);

			float3 up = { m._m10, m._m11, m._m12 };

			v0 = up * Min.y;
			v1 = up * Max.y;
			NewBB.Min += (std::min)(v0, v1);
			NewBB.Max += (std::max)(v0, v1);

			float3 back = { m._m20, m._m21, m._m22 };

			v0 = back * Min.z;
			v1 = back * Max.z;
			NewBB.Min += (std::min)(v0, v1);
			NewBB.Max += (std::max)(v0, v1);

			return NewBB;
		}

		float3 GetCorner(size_t i) const
		{
			return {
				(i & 0x01u) ? Max.x : Min.x,
				(i & 0x02u) ? Max.y : Min.y,
				(i & 0x04u) ? Max.z : Min.z,
			};
		}

		BoundBox Combine(const BoundBox& Box) const
		{
			return {
				(std::min)(Min, Box.Min),
				(std::max)(Max, Box.Max),
			};
		}

		BoundBox Enclose(const float3& Point) const
		{
			return {
				(std::min)(Min, Point),
				(std::max)(Max, Point),
			};
		}

		static const BoundBox Invalid()
		{
			return {
				float3::FMaxValue(),
				float3::FMinValue(),
			};
		}

		constexpr bool IsValid() const
		{
			return (Max.x >= Min.x &&
				Max.y >= Min.y &&
				Max.z >= Min.z);
		}

		explicit constexpr operator bool() const
		{
			return IsValid();
		}

		bool operator==(const BoundBox& rhs) const
		{
			return Min == rhs.Min && Max == rhs.Max;
		}

		bool operator!=(const BoundBox& rhs) const
		{
			return !(*this == rhs);
		}
	};

	struct OrientedBoundingBox
	{
		float3 Center;         // Center of the box
		float3 Axes[3];        // Normalized axes
		float  HalfExtents[3]; // Half extents along each axis
	};

	// Bounding box visibility
	enum class BoxVisibility
	{
		// Bounding box is guaranteed to be outside the view frustum
		//
		//                 .
		//             . ' |
		//         . '     |
		//       |         |
		//         .       |
		//       ___ ' .   |
		//      |   |    ' .
		//      |___|
		//
		Invisible,

		// Bounding box intersects the frustum
		//
		//                 .
		//             . ' |
		//         . '     |
		//       |         |
		//        _.__     |
		//       |   '|.   |
		//       |____|  ' .
		//
		Intersecting,

		// Bounding box is fully inside the view frustum
		//
		//                 .
		//             . ' |
		//         . '___  |
		//       |   |   | |
		//         . |___| |
		//           ' .   |
		//               ' .
		//
		FullyVisible
	};

	// Returns the nearest bounding box corner along the given direction
	inline float3 GetBoxNearestCorner(const float3& Direction, const BoundBox& Box)
	{
		return float3 //
		{
			(Direction.x > 0) ? Box.Min.x : Box.Max.x,
			(Direction.y > 0) ? Box.Min.y : Box.Max.y,
			(Direction.z > 0) ? Box.Min.z : Box.Max.z //
		};
	}

	// Returns the farthest bounding box corner along the given direction
	inline float3 GetBoxFarthestCorner(const float3& Direction, const BoundBox& Box)
	{
		return float3 //
		{
			(Direction.x > 0) ? Box.Max.x : Box.Min.x,
			(Direction.y > 0) ? Box.Max.y : Box.Min.y,
			(Direction.z > 0) ? Box.Max.z : Box.Min.z //
		};
	}

	// Tests if the bounding box is fully visible, intersecting or invisible with
	// respect to the plane.
	//
	// Plane normal doesn't have to be normalized.
	// The box is visible when it is in the positive halfspace of the plane.
	//
	//     Invisible    |        Visible
	//                  |   N
	//                  |===>
	//                  |
	//                  |
	inline BoxVisibility GetBoxVisibilityAgainstPlane(const Plane3D& Plane, const BoundBox& Box)
	{
		// Calculate the distance from the box center to the plane:
		//   Center = (Box.Max + Box.Min) * 0.5
		//   Distance = dot(Center, Plane.Normal) + Plane.Distance
		//            = dot(Box.Max + Box.Min, Plane.Normal) * 0.5 + Plane.Distance
		const float DistanceToCenter = float3::Dot(Box.Max + Box.Min, Plane.Normal) * 0.5f + Plane.Distance;

		// Calculate the projected half extents of the box onto the plane normal:
		const float ProjHalfLen = float3::Dot(Box.Max - Box.Min, float3::Abs(Plane.Normal)) * 0.5f;

		// Check if the box is completely outside the plane
		if (DistanceToCenter < -ProjHalfLen)
		{
			//       .        |
			//     .' '.      |   N
			//    '.   .'     |===>
			//      '.'       |
			//       |        |
			//       |<-------|
			//        Distance
			return BoxVisibility::Invisible;
		}

		// Check if the box is fully inside the plane
		if (DistanceToCenter > ProjHalfLen)
		{
			//     |            .
			//     |   N      .' '.
			//     |===>     '.   .'
			//     |           '.'
			//     |            |
			//     |----------->|
			//        Distance
			return BoxVisibility::FullyVisible;
		}

		return BoxVisibility::Intersecting;
	}

	// Tests if the oriented bounding box is fully visible, intersecting or invisible with
	// respect to the plane.
	inline BoxVisibility GetBoxVisibilityAgainstPlane(const Plane3D& Plane, const OrientedBoundingBox& Box)
	{
		// Calculate the distance from the box center to the plane
		float Distance = float3::Dot(Box.Center, Plane.Normal) + Plane.Distance;

		// Calculate the projected half extents of the box onto the plane normal
		float ProjHalfExtents =
			shz::Abs(float3::Dot(Box.Axes[0], Plane.Normal)) * Box.HalfExtents[0] +
			shz::Abs(float3::Dot(Box.Axes[1], Plane.Normal)) * Box.HalfExtents[1] +
			shz::Abs(float3::Dot(Box.Axes[2], Plane.Normal)) * Box.HalfExtents[2];

		// Check if the box is completely outside the plane
		if (Distance < -ProjHalfExtents)
		{
			//       .        |
			//     .' '.      |   N
			//    '.   .'     |===>
			//      '.'       |
			//       |        |
			//       |<-------|
			//        Distance
			return BoxVisibility::Invisible;
		}

		// Check if the box is fully inside the plane
		if (Distance > ProjHalfExtents)
		{
			//     |            .
			//     |   N      .' '.
			//     |===>     '.   .'
			//     |           '.'
			//     |            |
			//     |----------->|
			//        Distance
			return BoxVisibility::FullyVisible;
		}

		// Box intersects the plane
		return BoxVisibility::Intersecting;
	}


	// Flags must be listed in the same order as planes in the ViewFrustum struct:
	// LeftPlane, RightPlane, BottomPlane, TopPlane, NearPlane, FarPlane
	enum FRUSTUM_PLANE_FLAGS : uint32
	{
		FRUSTUM_PLANE_FLAG_NONE = 0x00,
		FRUSTUM_PLANE_FLAG_LEFT_PLANE = 1 << ViewFrustum::LEFT_PLANE_IDX,
		FRUSTUM_PLANE_FLAG_RIGHT_PLANE = 1 << ViewFrustum::RIGHT_PLANE_IDX,
		FRUSTUM_PLANE_FLAG_BOTTOM_PLANE = 1 << ViewFrustum::BOTTOM_PLANE_IDX,
		FRUSTUM_PLANE_FLAG_TOP_PLANE = 1 << ViewFrustum::TOP_PLANE_IDX,
		FRUSTUM_PLANE_FLAG_NEAR_PLANE = 1 << ViewFrustum::NEAR_PLANE_IDX,
		FRUSTUM_PLANE_FLAG_FAR_PLANE = 1 << ViewFrustum::FAR_PLANE_IDX,

		FRUSTUM_PLANE_FLAG_FULL_FRUSTUM = FRUSTUM_PLANE_FLAG_LEFT_PLANE |
		FRUSTUM_PLANE_FLAG_RIGHT_PLANE |
		FRUSTUM_PLANE_FLAG_BOTTOM_PLANE |
		FRUSTUM_PLANE_FLAG_TOP_PLANE |
		FRUSTUM_PLANE_FLAG_NEAR_PLANE |
		FRUSTUM_PLANE_FLAG_FAR_PLANE,

		FRUSTUM_PLANE_FLAG_OPEN_NEAR = FRUSTUM_PLANE_FLAG_LEFT_PLANE |
		FRUSTUM_PLANE_FLAG_RIGHT_PLANE |
		FRUSTUM_PLANE_FLAG_BOTTOM_PLANE |
		FRUSTUM_PLANE_FLAG_TOP_PLANE |
		FRUSTUM_PLANE_FLAG_FAR_PLANE
	};
	DEFINE_FLAG_ENUM_OPERATORS(FRUSTUM_PLANE_FLAGS);

	// Tests if bounding box is visible by the camera
	template <typename BoundBoxType>
	inline BoxVisibility GetBoxVisibility(const ViewFrustum& ViewFrustum,
		const BoundBoxType& Box,
		FRUSTUM_PLANE_FLAGS PlaneFlags = FRUSTUM_PLANE_FLAG_FULL_FRUSTUM)
	{
		int NumPlanesInside = 0;
		int TotalPlanes = 0;
		for (uint32 plane_idx = 0; plane_idx < ViewFrustum::NUM_PLANES; ++plane_idx)
		{
			if ((PlaneFlags & (1 << plane_idx)) == 0)
				continue;

			const Plane3D& CurrPlane = ViewFrustum.GetPlane(static_cast<ViewFrustum::PLANE_IDX>(plane_idx));

			BoxVisibility VisibilityAgainstPlane = GetBoxVisibilityAgainstPlane(CurrPlane, Box);

			// If bounding box is "behind" one of the planes, it is definitely invisible
			if (VisibilityAgainstPlane == BoxVisibility::Invisible)
				return BoxVisibility::Invisible;

			// Count total number of planes the bound box is inside
			if (VisibilityAgainstPlane == BoxVisibility::FullyVisible)
				++NumPlanesInside;

			++TotalPlanes;
		}

		return (NumPlanesInside == TotalPlanes) ? BoxVisibility::FullyVisible : BoxVisibility::Intersecting;
	}

	inline BoxVisibility GetBoxVisibility(const ViewFrustumExt& ViewFrustumExt,
		const BoundBox& Box,
		FRUSTUM_PLANE_FLAGS   PlaneFlags = FRUSTUM_PLANE_FLAG_FULL_FRUSTUM)
	{
		BoxVisibility Visibility = GetBoxVisibility(static_cast<const ViewFrustum&>(ViewFrustumExt), Box, PlaneFlags);
		if (Visibility == BoxVisibility::FullyVisible || Visibility == BoxVisibility::Invisible)
			return Visibility;

		if ((PlaneFlags & FRUSTUM_PLANE_FLAG_FULL_FRUSTUM) == FRUSTUM_PLANE_FLAG_FULL_FRUSTUM)
		{
			// Additionally test if the whole frustum is outside one of
			// the bounding box planes. This helps in the following situation:
			//
			//
			//       .
			//      /   '  .       .
			//     / AABB  /   . ' |
			//    /       /. '     |
			//       ' . / |       |
			//       * .   |       |
			//           ' .       |
			//               ' .   |
			//                   ' .

			// Test all frustum corners against every bound box plane
			for (int iBoundBoxPlane = 0; iBoundBoxPlane < 6; ++iBoundBoxPlane)
			{
				// struct BoundBox
				// {
				//     float3 Min;
				//     float3 Max;
				// };
				float CurrPlaneCoord = reinterpret_cast<const float*>(&Box)[iBoundBoxPlane];
				// Bound box normal is one of the axis, so we just need to pick the right coordinate
				int iCoordOrder = iBoundBoxPlane % 3; // 0, 1, 2, 0, 1, 2
				// Since plane normal is directed along one of the axis, we only need to select
				// if it is pointing in the positive (max planes) or negative (min planes) direction
				float fSign = (iBoundBoxPlane >= 3) ? +1.f : -1.f;
				bool  bAllCornersOutside = true;
				for (int iCorner = 0; iCorner < 8; iCorner++)
				{
					// Pick the frustum corner coordinate
					float CurrCornerCoord = ViewFrustumExt.FrustumCorners[iCorner][iCoordOrder];
					// Dot product is simply the coordinate difference multiplied by the sign
					if (fSign * (CurrPlaneCoord - CurrCornerCoord) > 0)
					{
						bAllCornersOutside = false;
						break;
					}
				}
				if (bAllCornersOutside)
					return BoxVisibility::Invisible;
			}
		}

		return BoxVisibility::Intersecting;
	}

	inline BoxVisibility GetBoxVisibility(const ViewFrustumExt& ViewFrustumExt,
		const OrientedBoundingBox& Box,
		FRUSTUM_PLANE_FLAGS        PlaneFlags = FRUSTUM_PLANE_FLAG_FULL_FRUSTUM)
	{
		BoxVisibility Visibility = GetBoxVisibility(static_cast<const ViewFrustum&>(ViewFrustumExt), Box, PlaneFlags);
		if (Visibility == BoxVisibility::FullyVisible || Visibility == BoxVisibility::Invisible)
			return Visibility;

		if ((PlaneFlags & FRUSTUM_PLANE_FLAG_FULL_FRUSTUM) == FRUSTUM_PLANE_FLAG_FULL_FRUSTUM)
		{
			// Test if the whole frustum is outside one of the bounding box planes.

			const float3 Corners[] =
			{
				ViewFrustumExt.FrustumCorners[0] - Box.Center,
				ViewFrustumExt.FrustumCorners[1] - Box.Center,
				ViewFrustumExt.FrustumCorners[2] - Box.Center,
				ViewFrustumExt.FrustumCorners[3] - Box.Center,
				ViewFrustumExt.FrustumCorners[4] - Box.Center,
				ViewFrustumExt.FrustumCorners[5] - Box.Center,
				ViewFrustumExt.FrustumCorners[6] - Box.Center,
				ViewFrustumExt.FrustumCorners[7] - Box.Center,
			};

			// Test all frustum corners against every box plane
			for (int iBoundBoxPlane = 0; iBoundBoxPlane < 6; ++iBoundBoxPlane)
			{
				const int    AxisIdx = iBoundBoxPlane / 2;
				const float3 Normal = Box.Axes[AxisIdx] * (iBoundBoxPlane & 0x01 ? -1.f : +1.f);

				bool bAllCornersOutside = true;
				for (int iCorner = 0; iCorner < 8; iCorner++)
				{
					float Dist = float3::Dot(Corners[iCorner], Normal) - Box.HalfExtents[AxisIdx];
					//
					//     _______
					//    |       |  N      .'
					//    |   |   |===>   .'
					//    |___|___|       '.
					//        |           | '.
					//        |---------->|
					//            Dist
					if (Dist < 0)
					{
						bAllCornersOutside = false;
						break;
					}
				}
				if (bAllCornersOutside)
					return BoxVisibility::Invisible;
			}
		}

		return BoxVisibility::Intersecting;
	}

	inline float GetPointToBoxDistanceSqr(const BoundBox& BB, const float3& Pos)
	{
		VERIFY_EXPR(BB.Max.x >= BB.Min.x &&
			BB.Max.y >= BB.Min.y &&
			BB.Max.z >= BB.Min.z);
		const float3 OffsetVec{
			float3::MaxComponent({Pos.x - BB.Max.x, BB.Min.x - Pos.x, 0.f}),
			float3::MaxComponent({Pos.y - BB.Max.y, BB.Min.y - Pos.y, 0.f}),
			float3::MaxComponent({Pos.z - BB.Max.z, BB.Min.z - Pos.z, 0.f}),
		};
		return float3::Dot(OffsetVec, OffsetVec);
	}

	inline float GetPointToBoxDistance(const BoundBox& BB, const float3& Pos)
	{
		return sqrt(GetPointToBoxDistanceSqr(BB, Pos));
	}

	inline float GetPointToBoxDistanceSqr(const OrientedBoundingBox& OBB, const float3& Pos)
	{
		const float3 RelPos = Pos - OBB.Center;

		const float Projs[3] =
		{
			float3::Dot(RelPos, OBB.Axes[0]),
			float3::Dot(RelPos, OBB.Axes[1]),
			float3::Dot(RelPos, OBB.Axes[2]),
		};
		const float3 OffsetVec{
			float3::MaxComponent({Projs[0] - OBB.HalfExtents[0], -OBB.HalfExtents[0] - Projs[0], 0.f}),
			float3::MaxComponent({Projs[1] - OBB.HalfExtents[1], -OBB.HalfExtents[1] - Projs[1], 0.f}),
			float3::MaxComponent({Projs[2] - OBB.HalfExtents[2], -OBB.HalfExtents[2] - Projs[2], 0.f}),
		};
		return float3::Dot(OffsetVec, OffsetVec);
	}

	inline float GetPointToBoxDistance(const OrientedBoundingBox& OBB, const float3& Pos)
	{
		return sqrt(GetPointToBoxDistanceSqr(OBB, Pos));
	}

	inline bool operator==(const Plane3D& p1, const Plane3D& p2) noexcept
	{
		return p1.Normal == p2.Normal &&
			p1.Distance == p2.Distance;
	}

	inline bool operator==(const ViewFrustum& f1, const ViewFrustum& f2) noexcept
	{

		return f1.LeftPlane == f2.LeftPlane &&
			f1.RightPlane == f2.RightPlane &&
			f1.BottomPlane == f2.BottomPlane &&
			f1.TopPlane == f2.TopPlane &&
			f1.NearPlane == f2.NearPlane &&
			f1.FarPlane == f2.FarPlane;

	}

	inline bool operator==(const ViewFrustumExt& f1, const ViewFrustumExt& f2) noexcept
	{
		if (!(static_cast<const ViewFrustum&>(f1) == static_cast<const ViewFrustum&>(f2)))
			return false;

		for (size_t c = 0; c < _countof(f1.FrustumCorners); ++c)
			if (f1.FrustumCorners[c] != f2.FrustumCorners[c])
				return false;

		return true;
	}

	template <typename T, typename Y>
	T HermiteSpline(T f0, // F(0)
		T f1, // F(1)
		T t0, // F'(0)
		T t1, // F'(1)
		Y x)
	{
		// https://en.wikipedia.org/wiki/Cubic_Hermite_spline
		Y x2 = x * x;
		Y x3 = x2 * x;
		return (2 * x3 - 3 * x2 + 1) * f0 + (x3 - 2 * x2 + x) * t0 + (-2 * x3 + 3 * x2) * f1 + (x3 - x2) * t1;
	}

	// Returns the minimum bounding sphere of a view frustum
	inline void GetFrustumMinimumBoundingSphere(float   Proj_00,   ///< cot(HorzFOV / 2)
		float   Proj_11,   ///< cot(VertFOV / 2) == proj_00 / AspectRatio
		float   NearPlane, ///< Near clip plane
		float   FarPlane,  ///< Far clip plane
		float3& Center,    ///< Sphere center == (0, 0, c)
		float& Radius     ///< Sphere radius
	)
	{
		// https://lxjk.github.io/2017/04/15/Calculate-Minimal-Bounding-Sphere-of-Frustum.html
		VERIFY_EXPR(FarPlane >= NearPlane);
		float k2 = 1.f / (Proj_00 * Proj_00) + 1.f / (Proj_11 * Proj_11);
		if (k2 > (FarPlane - NearPlane) / (FarPlane + NearPlane))
		{
			Center = float3(0, 0, FarPlane);
			Radius = FarPlane * std::sqrt(k2);
		}
		else
		{
			Center = float3(0, 0, 0.5f * (FarPlane + NearPlane) * (1 + k2));
			Radius = 0.5f * std::sqrt((FarPlane - NearPlane) * (FarPlane - NearPlane) + 2 * (FarPlane * FarPlane + NearPlane * NearPlane) * k2 + (FarPlane + NearPlane) * (FarPlane + NearPlane) * k2 * k2);
		}
	}

	// Intersects a ray with 3D box and computes distances to intersections
	inline bool IntersectRayBox3D(const float3& RayOrigin,
		const float3& RayDirection,
		float3        BoxMin,
		float3        BoxMax,
		float& EnterDist,
		float& ExitDist)
	{
		VERIFY_EXPR(RayDirection != float3(0, 0, 0));

		BoxMin -= RayOrigin;
		BoxMax -= RayOrigin;

		static constexpr float Epsilon = 1e-20f;

		float3 AbsRayDir = float3::Abs(RayDirection);
		float3 t_min //
		{
			AbsRayDir.x > Epsilon ? BoxMin.x / RayDirection.x : std::numeric_limits<float32>::max(),
			AbsRayDir.y > Epsilon ? BoxMin.y / RayDirection.y : std::numeric_limits<float32>::max(),
			AbsRayDir.z > Epsilon ? BoxMin.z / RayDirection.z : std::numeric_limits<float32>::max() //
		};
		float3 t_max //
		{
			AbsRayDir.x > Epsilon ? BoxMax.x / RayDirection.x : std::numeric_limits<float32>::lowest(),
			AbsRayDir.y > Epsilon ? BoxMax.y / RayDirection.y : std::numeric_limits<float32>::lowest(),
			AbsRayDir.z > Epsilon ? BoxMax.z / RayDirection.z : std::numeric_limits<float32>::lowest() //
		};

		EnterDist = float3::MaxComponent({ std::min(t_min.x, t_max.x), std::min(t_min.y, t_max.y), std::min(t_min.z, t_max.z) });
		ExitDist = float3::MaxComponent({ std::max(t_min.x, t_max.x), std::max(t_min.y, t_max.y), std::max(t_min.z, t_max.z) });

		// if ExitDist < 0, the ray intersects AABB, but the whole AABB is behind it
		// if EnterDist > ExitDist, the ray doesn't intersect AABB
		return ExitDist >= 0 && EnterDist <= ExitDist;
	}

	// Intersects a ray with the axis-aligned bounding box and computes distances to intersections
	inline bool IntersectRayAABB(const float3& RayOrigin,
		const float3& RayDirection,
		const BoundBox& AABB,
		float& EnterDist,
		float& ExitDist)
	{
		return IntersectRayBox3D(RayOrigin, RayDirection, AABB.Min, AABB.Max, EnterDist, ExitDist);
	}

	// Intersects a 2D ray with the 2D axis-aligned bounding box and computes distances to intersections
	inline bool IntersectRayBox2D(const float2& RayOrigin,
		const float2& RayDirection,
		float2        BoxMin,
		float2        BoxMax,
		float& EnterDist,
		float& ExitDist)
	{
		VERIFY_EXPR(RayDirection != float2(0, 0));

		BoxMin -= RayOrigin;
		BoxMax -= RayOrigin;

		static constexpr float Epsilon = 1e-20f;

		float2 AbsRayDir = float2::Abs(RayDirection);
		float2 t_min //
		{
			AbsRayDir.x > Epsilon ? BoxMin.x / RayDirection.x : std::numeric_limits<float32>::max(),
			AbsRayDir.y > Epsilon ? BoxMin.y / RayDirection.y : std::numeric_limits<float32>::max() //
		};
		float2 t_max //
		{
			AbsRayDir.x > Epsilon ? BoxMax.x / RayDirection.x : std::numeric_limits<float32>::lowest(),
			AbsRayDir.y > Epsilon ? BoxMax.y / RayDirection.y : std::numeric_limits<float32>::lowest() //
		};

		EnterDist = (std::max)((std::min)(t_min.x, t_max.x), (std::min)(t_min.y, t_max.y));
		ExitDist = (std::min)((std::max)(t_min.x, t_max.x), (std::max)(t_min.y, t_max.y));

		// if ExitDist < 0, the ray intersects AABB, but the whole AABB is behind it
		// if EnterDist > ExitDist, the ray doesn't intersect AABB
		return ExitDist >= 0 && EnterDist <= ExitDist;
	}


	// Intersects a ray with the triangle using Moller-Trumbore algorithm and returns
	// the distance along the ray to the intersection point.
	// If the intersection point is behind the ray origin, the distance will be negative.
	// If there is no intersection, returns std::numeric_limits<float32>::max().
	inline float IntersectRayTriangle(const float3& V0,
		const float3& V1,
		const float3& V2,
		const float3& RayOrigin,
		const float3& RayDirection,
		bool          CullBackFace = false)
	{
		float3 V0_V1 = V1 - V0;
		float3 V0_V2 = V2 - V0;

		float3 PVec = float3::Cross(RayDirection, V0_V2);

		float Det = float3::Dot(V0_V1, PVec);

		float t = std::numeric_limits<float32>::max();

		static constexpr float Epsilon = 1e-10f;
		// If determinant is near zero, the ray lies in the triangle plane
		if (Det > Epsilon || (!CullBackFace && Det < -Epsilon))
		{
			float3 V0_RO = RayOrigin - V0;

			// calculate U parameter and test bounds
			float u = float3::Dot(V0_RO, PVec) / Det;
			if (u >= 0 && u <= 1)
			{
				float3 QVec = float3::Cross(V0_RO, V0_V1);

				// calculate V parameter and test bounds
				float v = float3::Dot(RayDirection, QVec) / Det;
				if (v >= 0 && u + v <= 1)
				{
					// calculate t, ray intersects triangle
					t = float3::Dot(V0_V2, QVec) / Det;
				}
			}
		}

		return t;
	}


	// Traces a 2D line through the square cell grid and enumerates all cells the line touches.

	// \tparam TCallback - Type of the callback function.
	// \param f2Start    - Line start point.
	// \param f2End      - Line end point.
	// \param i2GridSize - Grid dimensions.
	// \param Callback   - Callback function that will be called with the argument of type int2
	//                     for every cell visited. The function should return true to continue
	//                     tracing and false to stop it.
	//
	// The algorithm clips the line against the grid boundaries [0 .. i2GridSize.x] x [0 .. i2GridSize.y]
	//
	// When one of the end points falls exactly on a vertical cell boundary, cell to the right is enumerated.
	// When one of the end points falls exactly on a horizontal cell boundary, cell above is enumerated.
	//
	// For example, for the line below on a 2x2 grid, the algorithm will trace the following cells: (0,0), (0,1), (1,1)
	//
	//                     End
	//                     /
	//        __________ _/________  2
	//       |          |/         |
	//       |          /          |
	//       |         /|          |
	//       |________/_|__________| 1
	//       |       /  |          |
	//       |      /   |          |
	//       |    Start |          |
	//       |__________|__________| 0
	//      0           1          2
	//
	template <typename TCallback>
	void TraceLineThroughGrid(float2    f2Start,
		float2    f2End,
		int2      i2GridSize,
		TCallback Callback)
	{
		VERIFY_EXPR(i2GridSize.x > 0 && i2GridSize.y > 0);
		const float2 f2GridSize = {static_cast<float32>(i2GridSize.x), static_cast<float32>(i2GridSize.y)};

		if (f2Start == f2End)
		{
			if (f2Start.x >= 0 && f2Start.x < f2GridSize.x &&
				f2Start.y >= 0 && f2Start.y < f2GridSize.y)
			{
				Callback({ static_cast<int>(f2Start.x), static_cast<int>(f2Start.y) });
			}
			return;
		}

		float2 f2Direction = f2End - f2Start;

		float EnterDist, ExitDist;
		if (IntersectRayBox2D(f2Start, f2Direction, float2{ 0, 0 }, f2GridSize, EnterDist, ExitDist))
		{
			f2End = f2Start + f2Direction * (std::min)(ExitDist, 1.f);
			f2Start = f2Start + f2Direction * (std::max)(EnterDist, 0.f);
			// Clamp start and end points to avoid FP precision issues
			f2Start = Vector2::Clamp(f2Start, float2{ 0, 0 }, f2GridSize);
			f2End = Vector2::Clamp(f2End, float2{ 0, 0 }, f2GridSize);

			const int   dh = f2Direction.x > 0 ? 1 : -1;
			const int   dv = f2Direction.y > 0 ? 1 : -1;
			const float p = f2Direction.y * f2Start.x - f2Direction.x * f2Start.y;
			const float tx = p - f2Direction.y * static_cast<float>(dh);
			const float ty = p + f2Direction.x * static_cast<float>(dv);

			const int2 i2End = { static_cast<int>(f2End.x), static_cast<int>(f2End.y) };
			VERIFY_EXPR(i2End.x >= 0 && i2End.y >= 0 && i2End.x <= i2GridSize.x && i2End.y <= i2GridSize.y);

			int2 i2Pos = { static_cast<int>(f2Start.x), static_cast<int>(f2Start.y) };
			VERIFY_EXPR(i2Pos.x >= 0 && i2Pos.y >= 0 && i2Pos.x <= i2GridSize.x && i2Pos.y <= i2GridSize.y);

			// Loop condition checks if we missed the end point of the line due to
			// floating point precision issues.
			// Normally we exit the loop when i2Pos == i2End.
			while ((i2End.x - i2Pos.x) * dh >= 0 &&
				(i2End.y - i2Pos.y) * dv >= 0)
			{
				if (i2Pos.x < i2GridSize.x && i2Pos.y < i2GridSize.y)
				{
					if (!Callback(i2Pos))
						break;
				}

				if (i2Pos.x == i2End.x && i2Pos.y == i2End.y)
				{
					// End of the line
					break;
				}
				else
				{
					// step to the next cell
					float t = f2Direction.x * (static_cast<float>(i2Pos.y) + 0.5f) - f2Direction.y * (static_cast<float>(i2Pos.x) + 0.5f);
					if (std::abs(t + tx) < std::abs(t + ty))
						i2Pos.x += dh;
					else
						i2Pos.y += dv;
				}
			}
		}
	}


	// Tests if a point is inside triangle.

	// \param [in] V0         - First triangle vertex
	// \param [in] V1         - Second triangle vertex
	// \param [in] V2         - Third triangle vertex
	// \param [in] Point      - Point to test
	// \param [in] AllowEdges - Whether to accept points lying on triangle edges
	// \return     true if the point lies inside the triangle,
	//             and false otherwise.
	bool IsPointInsideTriangle(
		const Vector2& V0,
		const Vector2& V1,
		const Vector2& V2,
		const Vector2& Point,
		bool bAllowEdges)
	{
		using DirType = Vector2;

		const DirType Rib[3] =
		{
			DirType
			{
				V1.x - V0.x,
				V1.y - V0.y,
			},
			DirType
			{
				V2.x - V1.x,
				V2.y - V1.y,
			},
			DirType
			{
				V0.x - V2.x,
				V0.y - V2.y,
			} 
		};

		const DirType VertToPoint[3] =
		{
			DirType
			{
				Point.x - V0.x,
				Point.y - V0.y
			},
			DirType
			{
				Point.x - V1.x,
				Point.y - V1.y
			},
			DirType //
			{
				Point.x - V2.x,
				Point.y - V2.y
			}                                                     
		};

		float32 NormalZ[3];
		for (int i = 0; i < 3; i++)
		{
			NormalZ[i] = Rib[i].x * VertToPoint[i].y - Rib[i].y * VertToPoint[i].x;
		}


		if (bAllowEdges)
		{
			return (NormalZ[0] >= 0 && NormalZ[1] >= 0 && NormalZ[2] >= 0) ||
				(NormalZ[0] <= 0 && NormalZ[1] <= 0 && NormalZ[2] <= 0);
		}
		else
		{
			return (NormalZ[0] > 0 && NormalZ[1] > 0 && NormalZ[2] > 0) ||
				(NormalZ[0] < 0 && NormalZ[1] < 0 && NormalZ[2] < 0);
		}

	}

	// Rasterizes a triangle and calls the callback function for every sample covered.

	// The samples are assumed to be located at integer coordinates. Samples located on
	// edges are always enumerated. Samples are enumerated row by row, bottom to top,
	// left to right. For example, for triangle (1, 1)-(1, 3)-(3, 1),
	// the following locations will be enumerated:
	// (1, 1), (2, 1), (3, 1), (1, 2), (2, 2), (1, 3).
	//
	//     3 *   *.  *   *
	//           | '.
	//     2 *   *   *.  *
	//           |     '.
	//     1 *   *---*---*
	//
	//     0 *   *   *   *
	//       0   1   2   3
	//
	// \tparam [in] T    - Vertex component type.
	// \tparam TCallback - Type of the callback function.
	//
	// \param [in] V0         - First triangle vertex.
	// \param [in] V1         - Second triangle vertex.
	// \param [in] V2         - Third triangle vertex.
	// \param [in] Callback   - Callback function that will be called with the argument of type int2
	//                          for every sample covered.
	template <class TCallback>
	void RasterizeTriangle(Vector2 V0,
		Vector2 V1,
		Vector2 V2,
		TCallback Callback)
	{
		if (V1.y < V0.y)
			std::swap(V1, V0);
		if (V2.y < V0.y)
			std::swap(V2, V0);
		if (V2.y < V1.y)
			std::swap(V2, V1);

		VERIFY_EXPR(V0.y <= V1.y && V1.y <= V2.y);

		const int iStartRow = static_cast<int>(std::ceilf(V0.y));
		const int iEndRow = static_cast<int>(std::floorf(V2.y));

		if (iStartRow == iEndRow)
		{
			int iStartCol = static_cast<int>(std::ceilf(Vector3::MinComponent(V0.x, V1.x, V2.x)));
			int iEndCol = static_cast<int>(std::floorf(Vector3::MaxComponent(V0.x, V1.x, V2.x)));
			for (int iCol = iStartCol; iCol <= iEndCol; ++iCol)
			{
				Callback(int2{ iCol, iStartRow });
			}
			return;
		}

		auto LerpCol = [](float32 StartCol, float32 EndCol, float32 StartRow, float32 EndRow, int CurrRow) //
			{
				return StartCol +
					((EndCol - StartCol) * (static_cast<float32>(CurrRow) - StartRow)) / (EndRow - StartRow);
			};

		for (int iRow = iStartRow; iRow <= iEndRow; ++iRow)
		{
			float32 dStartCol = LerpCol(V0.x, V2.x, V0.y, V2.y, iRow);

			float32 dEndCol;
			if (static_cast<float32>(iRow) < V1.y)
			{
				//                          V2.
				//    V2-------V1              \' .
				//     |     .'   <-            \   ' . V1
				//     |   .'     <-             \    /      <-
				//     | .'       <-              \  /       <-
				//     .'         <-               \/        <-
				//    V0          <-               V0        <-
				dEndCol = LerpCol(V0.x, V1.x, V0.y, V1.y, iRow);
			}
			else
			{
				if (V1.y < V2.y)
				{
					//                            V2.             <-
					//    V2            <-           \' .         <-
					//     |'.          <-            \   ' . V1  <-
					//     |  '.        <-             \    /
					//     |    '.      <-              \  /
					//     |      '.    <-               \/
					//    V0-------V1   <-               V0
					dEndCol = LerpCol(V1.x, V2.x, V1.y, V2.y, iRow);
				}
				else
				{
					//    V2-------V1   <-
					//     |     .'
					//     |   .'
					//     | .'
					//     .'
					//    V0
					dEndCol = V1.x;
				}
			}
			if (dStartCol > dEndCol)
				std::swap(dStartCol, dEndCol);

			int iStartCol = static_cast<int>(std::ceilf(dStartCol));
			int iEndCol = static_cast<int>(std::floorf(dEndCol));

			for (int iCol = iStartCol; iCol <= iEndCol; ++iCol)
			{
				Callback(int2{ iCol, iRow });
			}
		}
	}


	// Checks if two 2D-boxes overlap.

	// \tparam [in] AllowTouch - Whether to consider two boxes overlapping if
	//                           they only touch at their boundaries or corners.
	// \tparam [in] T          - Component type.
	//
	// \param [in]  Box0Min - Min corner of the first box.
	// \param [in]  Box0Max - Max corner of the first box.
	// \param [in]  Box1Min - Min corner of the second box.
	// \param [in]  Box1Max - Max corner of the second box.
	//
	// \return     true if the bounding boxes overlap, and false otherwise.
	template <bool bAllowTouch>
	bool CheckBox2DBox2DOverlap(
		const Vector2& Box0Min,
		const Vector2& Box0Max,
		const Vector2& Box1Min,
		const Vector2& Box1Max)
	{
		VERIFY_EXPR(Box0Max.x >= Box0Min.x && Box0Max.y >= Box0Min.y &&
			Box1Max.x >= Box1Min.x && Box1Max.y >= Box1Min.y);
		if (bAllowTouch)
		{
			return !(Box0Min.x > Box1Max.x || Box1Min.x > Box0Max.x || Box0Min.y > Box1Max.y || Box1Min.y > Box0Max.y);
		}
		else
		{
			return !(Box0Min.x >= Box1Max.x || Box1Min.x >= Box0Max.x || Box0Min.y >= Box1Max.y || Box1Min.y >= Box0Max.y);
		}
	}


	// Checks if two 1D-line sections overlap.

	// \tparam [in] AllowTouch - Whether to consider two sections overlapping if
	//                           they only touch at their end points.
	// \tparam [in] T          - Component type.
	//
	// \param [in]  Min0 - Min end point of the first section
	// \param [in]  Max0 - Max end point of the first section
	// \param [in]  Min1 - Min end point of the second section
	// \param [in]  Max1 - Max end point of the second section
	//
	// \return     true if the sections overlap, and false otherwise.
	template <bool AllowTouch, typename T>
	bool CheckLineSectionOverlap(T Min0, T Max0, T Min1, T Max1)
	{
		VERIFY_EXPR(Min0 <= Max0 && Min1 <= Max1);
		//     [------]         [------]
		//   Min0    Max0    Min1     Max1
		//
		//     [------]         [------]
		//   Min1    Max1    Min0     Max0
		if (AllowTouch)
		{
			return !(Min0 > Max1 || Min1 > Max0);
		}
		else
		{
			return !(Min0 >= Max1 || Min1 >= Max0);
		}
	}

	// Triangulation result flags returned by the polygon triangulator.
	enum TRIANGULATE_POLYGON_RESULT : uint32
	{
		// The polygon was triangulated successfully.
		TRIANGULATE_POLYGON_RESULT_OK = 0,

		// The polygon contains less than three vertices.
		TRIANGULATE_POLYGON_RESULT_TOO_FEW_VERTS = 1u << 0u,

		// All polygon vertices are collinear.
		TRIANGULATE_POLYGON_RESULT_VERTS_COLLINEAR = 1u << 1u,

		// Convex vertex is not outside of the polygon.
		//
		// \note   This flag may be set due to floating point imprecision
		//         if there are (almost) collinear vertices.
		TRIANGULATE_POLYGON_RESULT_INVALID_CONVEX = 1u << 2u,

		// Ear vertex is not outside of the polygon.
		//
		// \note   This flag may be set due to floating point imprecision
		//         if there are (almost) collinear vertices.
		TRIANGULATE_POLYGON_RESULT_INVALID_EAR = 1u << 3u,

		// No ear vertex was found at one of the steps.
		TRIANGULATE_POLYGON_RESULT_NO_EAR_FOUND = 1u << 4u
	};
	DEFINE_FLAG_ENUM_OPERATORS(TRIANGULATE_POLYGON_RESULT);

	// 2D polygon triangulator.
	//
	// The class implements the ear-clipping algorithm to triangulate simple (i.e.
	// non-self-intersecting) 2D polygons.
	//
	// \tparam IndexType - Index type (e.g. uint32 or uint16).
	template <typename IndexType>
	class Polygon2DTriangulator
	{
	public:
		// Triangulates a simple polygon using the ear-clipping algorithm.

		// \param [in]  Polygon   - A list of polygon vertices. The last vertex is
		//                          assumed to be connected to the first one
		//
		// \return     The triangle list.
		//
		// The winding order of each triangle is the same as the winding
		// order of the polygon.
		//
		// The function does not check if the polygon is simple, e.g.
		// that it does not self-intersect.
		const std::vector<IndexType>& Triangulate(const std::vector<Vector2>& Polygon)
		{
			m_Result = TRIANGULATE_POLYGON_RESULT_OK;
			m_Triangles.clear();

			const int VertCount = static_cast<int>(Polygon.size());
			if (VertCount <= 2)
			{
				m_Result = TRIANGULATE_POLYGON_RESULT_TOO_FEW_VERTS;
				return m_Triangles;
			}

			const int TriangleCount = VertCount - 2;
			if (TriangleCount == 1)
			{
				m_Triangles = { 0, 1, 2 };
				return m_Triangles;
			}

			// Find the leftmost vertex to determine the winding order
			int LeftmostVertIdx = 0;
			for (int i = 1; i < VertCount; ++i)
			{
				if (Polygon[i].x < Polygon[LeftmostVertIdx].x)
					LeftmostVertIdx = i;
			}

			auto WrapIndex = [](int idx, int Count) {
				return ((idx % Count) + Count) % Count;
				};

			// Returns the winding order of the triangle formed by the given vertices.
			//
			//    V0    V2
			//      \  /
			//       \/
			//       V1
			auto GetWinding = [](const auto& V0, const auto& V1, const auto& V2) {
				return (V1.x - V0.x) * (V2.y - V1.y) - (V2.x - V1.x) * (V1.y - V0.y);
				};

			// Find the winding order of the polygon
			float32 PolygonWinding = 0;
			// Handle the case when the leftmost vertex is collinear with its neighbors:
			// *.
			// | '.
			// |   '.
			// *    .*
			// |  .'
			// |.'
			// *
			for (int i = 0; i < VertCount&& PolygonWinding == 0; ++i)
			{
				const auto& V0 = Polygon[WrapIndex(LeftmostVertIdx + i - 1, VertCount)];
				const auto& V1 = Polygon[WrapIndex(LeftmostVertIdx + i + 0, VertCount)];
				const auto& V2 = Polygon[WrapIndex(LeftmostVertIdx + i + 1, VertCount)];
				PolygonWinding = GetWinding(V0, V1, V2);
			}
			if (PolygonWinding == 0)
			{
				m_Result = TRIANGULATE_POLYGON_RESULT_VERTS_COLLINEAR;
				return m_Triangles;
			}
			PolygonWinding = PolygonWinding > float32{ 0 } ? float32{ 1 } : float32{ -1 };

			m_RemainingVertIds.resize(VertCount);
			m_VertTypes.resize(VertCount);
			for (int i = 0; i < VertCount; ++i)
			{
				m_RemainingVertIds[i] = i;
				m_VertTypes[i] = VertexType::Convexx;
			}

			auto CheckConvex = [&](int vert_id) {
				const int RemainingVertCount = static_cast<int>(m_RemainingVertIds.size());

				const int Idx0 = m_RemainingVertIds[WrapIndex(vert_id - 1, RemainingVertCount)];
				const int Idx1 = m_RemainingVertIds[WrapIndex(vert_id + 0, RemainingVertCount)];
				const int Idx2 = m_RemainingVertIds[WrapIndex(vert_id + 1, RemainingVertCount)];

				const auto& V0 = Polygon[Idx0];
				const auto& V1 = Polygon[Idx1];
				const auto& V2 = Polygon[Idx2];

				return GetWinding(V0, V1, V2) * PolygonWinding < 0 ?
					VertexType::Reflex :
					VertexType::Convexx;
				};

			auto CheckEar = [&](int vert_id) {
				const int RemainingVertCount = static_cast<int>(m_RemainingVertIds.size());

				const int Idx0 = m_RemainingVertIds[WrapIndex(vert_id - 1, RemainingVertCount)];
				const int Idx1 = m_RemainingVertIds[WrapIndex(vert_id + 0, RemainingVertCount)];
				const int Idx2 = m_RemainingVertIds[WrapIndex(vert_id + 1, RemainingVertCount)];

				VERIFY_EXPR(m_VertTypes[Idx1] == VertexType::Convexx);

				const auto& V0 = Polygon[Idx0];
				const auto& V1 = Polygon[Idx1];
				const auto& V2 = Polygon[Idx2];

				for (const int Idx : m_RemainingVertIds)
				{
					if (Idx == Idx0 || Idx == Idx1 || Idx == Idx2)
						continue;

					if (m_VertTypes[Idx] == VertexType::Convexx || m_VertTypes[Idx] == VertexType::Ear)
					{
#ifdef SHZ_DEBUG
						// This check may fail due to floating point imprecision if there are collinear vertices.
						if (IsPointInsideTriangle(V0, V1, V2, Polygon[Idx], /*AllowEdges = */ false))
						{
							// Convex and ear vertices must always be outside the triangle
							m_Result |= (m_VertTypes[Idx] == VertexType::Convexx) ?
								TRIANGULATE_POLYGON_RESULT_INVALID_CONVEX :
								TRIANGULATE_POLYGON_RESULT_INVALID_EAR;
						}
#endif
						continue;
					}

					// Do not treat vertices exactly on the edge as inside the triangle,
					// so that we can clip out degenerate triangles.
					if (IsPointInsideTriangle(V0, V1, V2, Polygon[Idx], /*AllowEdges = */ false))
					{
						// The vertex is inside the triangle
						return VertexType::Convexx;
					}
				}

				return VertexType::Ear;
				};

			// First label vertices as reflex or convex
			for (int vert_id = 0; vert_id < VertCount; ++vert_id)
			{
				m_VertTypes[vert_id] = CheckConvex(vert_id);
			}

			// Next, check convex vertices for ears
			for (int vert_id = 0; vert_id < VertCount; ++vert_id)
			{
				VertexType& VertType = m_VertTypes[vert_id];
				if (VertType == VertexType::Convexx)
					VertType = CheckEar(vert_id);
			}

			m_Triangles.clear();
			m_Triangles.reserve(TriangleCount * 3);

			// Clip ears one by one until only three vertices are left
			while (m_RemainingVertIds.size() > 3)
			{
				int RemainingVertCount = static_cast<int>(m_RemainingVertIds.size());

				// Find the first ear
				int ear_vert_id = 0;
				for (; ear_vert_id < RemainingVertCount; ++ear_vert_id)
				{
					const int Idx = m_RemainingVertIds[ear_vert_id];
					if (m_VertTypes[Idx] == VertexType::Ear)
						break;
				};

				if (ear_vert_id == RemainingVertCount)
				{
					// No ears found
					m_Result |= TRIANGULATE_POLYGON_RESULT_NO_EAR_FOUND;
					ear_vert_id = 0;
				}

				const int Idx0 = m_RemainingVertIds[WrapIndex(ear_vert_id - 1, RemainingVertCount)];
				const int Idx1 = m_RemainingVertIds[ear_vert_id];
				const int Idx2 = m_RemainingVertIds[WrapIndex(ear_vert_id + 1, RemainingVertCount)];

				m_Triangles.emplace_back(Idx0);
				m_Triangles.emplace_back(Idx1);
				m_Triangles.emplace_back(Idx2);
				m_RemainingVertIds.erase(m_RemainingVertIds.begin() + ear_vert_id);

				--RemainingVertCount;
				// Update adjacent vertices
				if (RemainingVertCount > 3)
				{
					const int IdxL = m_RemainingVertIds[WrapIndex(ear_vert_id - 1, RemainingVertCount)];
					const int IdxR = m_RemainingVertIds[WrapIndex(ear_vert_id, RemainingVertCount)];
					// First check for convex vs reflex
					m_VertTypes[IdxL] = CheckConvex(ear_vert_id - 1);
					m_VertTypes[IdxR] = CheckConvex(ear_vert_id);

					// Next, check for ears
					if (m_VertTypes[IdxL] == VertexType::Convexx)
						m_VertTypes[IdxL] = CheckEar(ear_vert_id - 1);
					if (m_VertTypes[IdxR] == VertexType::Convexx)
						m_VertTypes[IdxR] = CheckEar(ear_vert_id);
				}
			}

			m_Triangles.emplace_back(m_RemainingVertIds[0]);
			m_Triangles.emplace_back(m_RemainingVertIds[1]);
			m_Triangles.emplace_back(m_RemainingVertIds[2]);

			return m_Triangles;
		}

		TRIANGULATE_POLYGON_RESULT GetResult() const { return m_Result; }

	protected:
		TRIANGULATE_POLYGON_RESULT m_Result = TRIANGULATE_POLYGON_RESULT_OK;
		std::vector<IndexType>     m_Triangles;

	private:
		//        Reflex
		//   Ear.   |   .Ear
		//      \'. V .'/
		//       \ '.' /
		//        \   /
		//         \ /
		//          V
		//       Convex
		//
		enum class VertexType : uint8
		{
			Convexx, // X11 #defines 'Convex'
			Reflex,
			Ear
		};
		std::vector<VertexType> m_VertTypes;

		// Remaining vertices to process
		std::vector<int> m_RemainingVertIds;
	};


	// 3D polygon triangulator.

	// The class extends the Polygon2DTriangulator class to handle simple 3D polygons.
	// It first projects the polygon onto a plane and then triangulates the resulting 2D polygon.
	//
	// \tparam IndexType     - Index type (e.g. uint32 or uint16).
	template <typename IndexType>
	class Polygon3DTriangulator : public Polygon2DTriangulator<typename std::enable_if<std::is_floating_point<float32>::value, IndexType>::type>
	{
	public:
		// Triangulates a simple polygon in 3D.

		// The function first projects the polygon onto a plane and then
		// triangulates the resulting 2D polygon.
		//
		// If vertices are not coplanar, the result is undefined.
		const std::vector<IndexType>& Triangulate(const std::vector<Vector3>& Polygon)
		{
			this->m_Triangles.clear();

			// Find the mean normal.
			Vector3 Normal;
			for (size_t i = 0; i < Polygon.size(); ++i)
			{
				const auto& V0 = Polygon[i];
				const auto& V1 = Polygon[(i + 1) % Polygon.size()];
				const auto& V2 = Polygon[(i + 2) % Polygon.size()];

				const auto VertexNormal = Vector3::Cross(V1 - V0, V2 - V1);

				// Align current normal with the mean normal to handle both convex and reflex vertices
				const float32 Sign = Vector3::Dot(Normal, VertexNormal) >= 0 ? float32{ 1 } : float32{ -1 };
				Normal += Sign * VertexNormal;
			}

			if (Normal == Vector3{})
			{
				this->m_Result = TRIANGULATE_POLYGON_RESULT_VERTS_COLLINEAR;
				return this->m_Triangles;
			}
			const auto AbsNormal = Vector3::Abs(Normal);

			Vector3 Tangent;
			if (AbsNormal.z > (std::max)(AbsNormal.x, AbsNormal.y))
				Tangent = Vector3::Cross(Vector3{0.f, 1.f, 0.f }, Normal);
			else if (AbsNormal.y > (std::max)(AbsNormal.x, AbsNormal.z))
				Tangent = Vector3::Cross(Vector3{1.f, 0.f, 0.f }, Normal);
			else
				Tangent = Vector3::Cross(Vector3{ 0.f, 0.f, 1.f}, Normal);
			VERIFY_EXPR(Vector3::Length(Tangent) > 0);
			Tangent = Vector3::Normalize(Tangent);

			auto Bitangent = Vector3::Cross(Normal, Tangent);
			VERIFY_EXPR(Vector3::Length(Bitangent) > 0);
			Bitangent = Vector3::Normalize(Bitangent);

			// Project the polygon
			m_PolygonProj.clear();
			m_PolygonProj.reserve(Polygon.size());
			for (const auto& Vert : Polygon)
			{
				m_PolygonProj.emplace_back(Vector3::Dot(Tangent, Vert), Vector3::Dot(Bitangent, Vert));
			}

			return Polygon2DTriangulator<IndexType>::Triangulate(m_PolygonProj);
		}

	private:
		std::vector<Vector2> m_PolygonProj;
	};

} // namespace shz

namespace std
{

	template <>
	struct hash<shz::Plane3D>
	{
		size_t operator()(const shz::Plane3D& Plane) const
		{
			return shz::ComputeHash(Plane.Normal, Plane.Distance);
		}
	};

	template <>
	struct hash<shz::ViewFrustum>
	{
		size_t operator()(const shz::ViewFrustum& Frustum) const
		{
			return shz::ComputeHash(Frustum.LeftPlane, Frustum.RightPlane, Frustum.BottomPlane, Frustum.TopPlane, Frustum.NearPlane, Frustum.FarPlane);
		}
	};

	template <>
	struct hash<shz::ViewFrustumExt>
	{
		size_t operator()(const shz::ViewFrustumExt& Frustum) const
		{
			size_t Seed = shz::ComputeHash(static_cast<const shz::ViewFrustum&>(Frustum));
			for (size_t Corner = 0; Corner < _countof(Frustum.FrustumCorners); ++Corner)
				shz::HashCombine(Seed, Frustum.FrustumCorners[Corner]);
			return Seed;
		}
	};

} // namespace std
