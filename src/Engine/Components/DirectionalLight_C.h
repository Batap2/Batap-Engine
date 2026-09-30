#pragma once

#include "EigenTypes.h"
#include "Reflection/ComponentRegistry.h"
#include "Systems/CascadeLight_S.h"

namespace batap
{
struct DirectionalLight_C
{
    col3 color_ = {1, 1, 1};
    float intensity_ = 1;
    bool castShadows_ = true;
    float sourceAngle_ = 0.53f;
};

template <>
struct ComponentAdmission<DirectionalLight_C>
{
    static std::string refusal(const entt::registry& reg, entt::entity e)
    {
        return CascadeLight_S::refusal(reg, e, "DirectionalLight_C");
    }
};

BATAP_COMPONENT(DirectionalLight_C, "directionalLight",
                ComponentMeta{.color = ComponentColor::Orange});
}  // namespace batap
