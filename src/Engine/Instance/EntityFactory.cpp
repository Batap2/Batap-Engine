#include "EntityFactory.h"
#include "Components/EntityHandle.h"
#include "Components/Name_C.h"
#include "Instance/Spawnable.h"
#include "Reflection/ComponentRegistry.h"
#include "Systems/Hierarchy_S.h"

#include <string>
#include <vector>

namespace batap
{
EntityHandle EntityFactory::create(entt::registry& reg, const Spawnable& spawnable)
{
    const std::string why = spawnable.refusal ? spawnable.refusal(reg, entt::null) : std::string{};
    if (!why.empty())
    {
        logRefusal(why);
        return {};
    }

    auto entity = reg.create();
    reg.emplace<Name_C>(entity, spawnable.label);
    if (spawnable.emplace)
        spawnable.emplace(reg, entity);

    return {&reg, entity};
}

void EntityFactory::destroy(EntityHandle h)
{
    auto& reg = *h.reg_;
    if (!reg.valid(h.entity_))
        return;

    // Collect children before modifying hierarchy
    std::vector<entt::entity> childList;
    for (entt::entity child : Hierarchy_S::children(h))
        childList.push_back(child);

    for (auto child : childList)
        destroy({&reg, child});

    Hierarchy_S::detach(h);
    reg.destroy(h.entity_);
}
}  // namespace batap
