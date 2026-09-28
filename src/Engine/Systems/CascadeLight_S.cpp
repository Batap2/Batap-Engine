#include "Systems/CascadeLight_S.h"

#include "Components/FarPointLight_C.h"
#include "Components/Name_C.h"

#include <iostream>
#include <string>

namespace batap
{
namespace
{
std::string describe(const entt::registry& reg, entt::entity e)
{
    if (e == entt::null)
        return "a new entity";
    if (const auto* name = reg.try_get<Name_C>(e); name && !name->name_.empty())
        return "'" + name->name_ + "'";
    return "entity " + std::to_string(entt::to_integral(e));
}

entt::entity holderOtherThan(const entt::registry& reg, entt::entity e)
{
    for (entt::entity other : reg.view<const FarPointLight_C>())
        if (other != e)
            return other;
    return entt::null;
}
}  // namespace

std::string CascadeLight_S::refusal(const entt::registry& reg, entt::entity e, std::string_view type)
{
    const entt::entity holder = holderOtherThan(reg, e);
    if (holder == entt::null)
        return {};
    return std::string(type) + " refused on " + describe(reg, e) + ": " + describe(reg, holder) +
           " already drives the cascades, and a scene has at most one FarPointLight_C or "
           "DirectionalLight_C.";
}

entt::entity CascadeLight_S::find(const entt::registry& reg)
{
    return holderOtherThan(reg, entt::null);
}

void CascadeLight_S::connectHooks(entt::registry& reg)
{
    reg.on_construct<FarPointLight_C>().connect<&CascadeLight_S::onConstruct>(*this);
}

void CascadeLight_S::onConstruct(entt::registry& reg, entt::entity e)
{
    const entt::entity holder = holderOtherThan(reg, e);
    if (holder == entt::null)
        return;
    std::cerr << "[CascadeLight] FarPointLight_C emplaced on " << describe(reg, e)
              << " without going through the engine's checks, while " << describe(reg, holder)
              << " already drives the cascades: removed at the start of the next frame.\n";
    refused_.push_back(e);
}

void CascadeLight_S::enforce(entt::registry& reg)
{
    for (entt::entity e : refused_)
        if (reg.valid(e))
            reg.remove<FarPointLight_C>(e);
    refused_.clear();
}
}  // namespace batap
