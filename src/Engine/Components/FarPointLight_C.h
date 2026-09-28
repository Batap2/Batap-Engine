#pragma once

#include "EigenTypes.h"
#include "Reflection/ComponentRegistry.h"
#include "Systems/CascadeLight_S.h"

namespace batap
{
// At most one per scene, directional lights included: CascadeLight_S.
struct FarPointLight_C
{
    col3 color_ = {1, 1, 1};
    float intensity_ = 1;
    float radius_ = 40000;
    float falloff_ = 1;
    bool castShadows_ = true;

    float sourceRadius_ = 0;
};

template <>
struct ComponentAdmission<FarPointLight_C>
{
    static std::string refusal(const entt::registry& reg, entt::entity e)
    {
        return CascadeLight_S::refusal(reg, e, "FarPointLight_C");
    }
};

BATAP_COMPONENT(FarPointLight_C, "farPointLight", ComponentMeta{.color = ComponentColor::Orange});
}  // namespace batap
