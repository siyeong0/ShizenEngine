#pragma once
#include <vector>
#include "Engine/Core/Math/Math.h"

namespace shz
{
    struct View
    {
        float3 CameraPosition;

        Matrix4x4 ViewMatrix;
        Matrix4x4 ProjMatrix;
        Matrix4x4 ViewProjMatrix;

        Matrix4x4 PrevViewMatrix;
        Matrix4x4 PrevProjMatrix;
        Matrix4x4 PrevViewProjMatrix;

		float FieldOfViewY;
		float AspectRatio;

        Rect Viewport;
        float32 NearPlane;
        float32 FarPlane;

		bool bOrthographic;
		float OrthographicSize;
    };

    struct ViewFamily
    {
        std::vector<View> Views;

        float DeltaTime;
        float CurrentTime;
        uint64 FrameIndex;

        uint32 ShowFlags;
    };
}