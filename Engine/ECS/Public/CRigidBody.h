#pragma once
#include "Engine/ECS/Public/Common.h"

namespace shz
{
    enum class ERigidBodyMotion : uint8
    {
        Static = 0,
        Dynamic,
        Kinematic,
    };

    COMPONENT CRigidbody final
    {
        ERigidBodyMotion Motion = ERigidBodyMotion::Static;
        uint8 Layer = 0; // 0: NonMoving, 1: Moving

        float Mass = 1.0f;
        float LinearDamping = 0.0f;
        float AngularDamping = 0.0f;
        bool bAllowSleeping = true;
        bool bEnableGravity = true;
        bool bStartActive = true;

        // Runtime (owned by PhysicsSystem)
        uint32 BodyHandle = 0; // PhysicsBodyHandle::Value
    };
} // namespace shz