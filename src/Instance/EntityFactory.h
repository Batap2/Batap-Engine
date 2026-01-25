#pragma once

#include "Components/EntityHandle.h"
#include "Handles.h"

#include <entt/entt.hpp>

namespace rayvox
{

    struct GPUInstanceManager;

    struct EntityFactory{

        GPUInstanceManager& _instanceManager;

        EntityFactory(GPUInstanceManager& instanceManager);
        EntityHandle createStaticMesh(entt::registry& reg, AssetHandle handle);
        EntityHandle createCamera(entt::registry& reg);
    };
}  // namespace rayvox
