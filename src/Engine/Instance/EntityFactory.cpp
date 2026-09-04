#include "EntityFactory.h"
#include "Components/EntityHandle.h"
#include "Components/Kind_C.h"
#include "Components/Name_C.h"
#include "Instance/InstanceManager.h"
#include "Instance/Spawnable.h"
#include "Systems/Hierarchy_S.h"

#include <vector>

namespace batap
{
EntityFactory::EntityFactory(GPUInstanceManager& instanceManager)
    : instanceManager_(instanceManager)
{}

EntityHandle EntityFactory::create(entt::registry& reg, const Spawnable& spawnable)
{
    auto entity = reg.create();
    reg.emplace<Name_C>(entity, spawnable.label);
    reg.emplace<Kind_C>(entity, spawnable.kind);
    if (spawnable.emplace)
        spawnable.emplace(reg, entity);

    EntityHandle h{&reg, entity};
    instanceManager_.visitPool(spawnable.kind, [&](auto& pool) { pool.insert(h); });
    return h;
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

    if (auto* k = reg.try_get<Kind_C>(h.entity_))
        instanceManager_.visitPool(k->value, [&](auto& pool) { pool.remove(h); });

    reg.destroy(h.entity_);
}
}  // namespace batap
