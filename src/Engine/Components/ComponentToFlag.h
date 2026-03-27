#pragma once

#include "Components/ComponentFlag.h"
#include "Components/Materials_C.h"
#include "Components/Transform_C.h"

namespace batap
{
template <typename C>
struct ComponentToFlag
{
    static_assert(sizeof(C) == 0, "ComponentToFlag not specialized for this component type");
};

template <>
struct ComponentToFlag<Transform_C>
{
    static constexpr ComponentFlag value = ComponentFlag::Transform;
};

template <>
struct ComponentToFlag<Materials_C>
{
    static constexpr ComponentFlag value = ComponentFlag::Materials;
};
}  // namespace batap
