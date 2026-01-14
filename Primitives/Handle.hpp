#pragma once
#include "Primitives/BasicTypes.h"

namespace shz
{
    template<typename T>
    struct Handle final
    {
        uint32 Id = 0;

        constexpr Handle() noexcept = default;
        constexpr explicit Handle(uint32 id) noexcept : Id(id) {}

        constexpr bool IsValid() const noexcept { return Id != 0; }

        friend constexpr bool operator==(const Handle& a, const Handle& b) noexcept { return a.Id == b.Id; }
        friend constexpr bool operator!=(const Handle& a, const Handle& b) noexcept { return a.Id != b.Id; }
    };
}

namespace std
{
    template<typename T>
    struct hash<shz::Handle<T>>
    {
        size_t operator()(const shz::Handle<T>& h) const noexcept
        {
            return std::hash<uint32_t>{}(h.Id);
        }
    };
}