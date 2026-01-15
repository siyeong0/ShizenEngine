#pragma once
#include <vector>
#include "Engine/Core/Math/Math.h"
#include "RenderTarget.h"

namespace shz
{
    struct View
    {
        Matrix4x4 ViewMatrix;
        Matrix4x4 ProjMatrix;
        Rect Viewport;
        float32 NearPlane;
        float32 FarPlane;
    };

    struct ViewFamily
    {
        RenderTarget Target;
        std::vector<View> Views;

        float DeltaTime;
        float CurrentTime;
        uint64 FrameIndex;

        uint32 ShowFlags;
    };
}