#pragma once

#include <cstdint>

namespace batap
{
enum class ComponentFlag : uint32_t
{
    None = 0,
    Transform = 1u << 0,
    Camera = 1u << 1,
};

constexpr ComponentFlag operator|(ComponentFlag a, ComponentFlag b)
{
    return static_cast<ComponentFlag>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

constexpr ComponentFlag operator&(ComponentFlag a, ComponentFlag b)
{
    return static_cast<ComponentFlag>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

constexpr ComponentFlag& operator|=(ComponentFlag& a, ComponentFlag b)
{
    a = a | b;
    return a;
}

constexpr bool any(ComponentFlag f)
{
    return static_cast<uint32_t>(f) != 0;
}

}  // namespace batap
