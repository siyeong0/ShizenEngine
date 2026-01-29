#pragma once
#include "Primitives/BasicTypes.h"
#include "Engine/Physics/Public/PhysicsBodyHandle.h"

namespace shz
{
    enum class EContactEventType : uint8
    {
        Added,
        Persisted,
        Removed
    };

    struct ContactEvent final
    {
        EContactEventType Type = EContactEventType::Added;

        PhysicsBodyHandle BodyA = {};
        PhysicsBodyHandle BodyB = {};

        float3 NormalWS = { 0, 1, 0 };
        float PenetrationDepth = 0.0f;

        bool bSensor = false;
    };
}
