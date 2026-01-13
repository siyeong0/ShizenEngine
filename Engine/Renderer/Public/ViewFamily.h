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
    };

    struct ViewFamily
    {
        RenderTarget Target;
        std::vector<View> Views;

        float DeltaTime;
        uint64 FrameIndex;

        uint32 ShowFlags;
    };
}