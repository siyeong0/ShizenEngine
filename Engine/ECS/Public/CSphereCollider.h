#pragma once
#include "Engine/ECS/Public/Common.h"

namespace shz
{
    COMPONENT CSphereCollider final
    {
        float Radius = 0.5f;
        float3 Center = { 0, 0, 0 };
        bool bIsSensor = false;

        uint64 ShapeHandle = 0;
    };
} // name