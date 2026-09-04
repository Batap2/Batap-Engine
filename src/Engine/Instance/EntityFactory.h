#pragma once

#include "Components/EntityHandle.h"
#include "Instance/Spawnable.h"

#include <entt/entt.hpp>

namespace batap
{

struct GPUInstanceManager;

struct EntityFactory
{
    GPUInstanceManager& instanceManager_;

    EntityFactory(GPUInstanceManager& instanceManager);
    EntityHandle create(entt::registry& reg, const Spawnable& spawnable);
    void destroy(EntityHandle h);
};
}  // namespace batap
