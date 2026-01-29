#pragma once
#include "Primitives/BasicTypes.h"

namespace shz
{
    struct PhysicsBodyHandle final
    {
        uint32 Value = 0; // 0 = invalid
        constexpr bool IsValid() const noexcept { return Value != 0; }
    };

    struct PhysicsShapeHandle final
    {
        uint64 Value = 0; // 0 = invalid
        constexpr bool IsValid() const noexcept { return Value != 0; }
    };
}