#pragma once

#include "Components/EntityHandle.h"
#include "Assets/AssetHandle.h"

#include <entt/entt.hpp>
#include <optional>

namespace batap
{

struct GPUInstanceManager;

struct EntityFactory
{
    GPUInstanceManager& _instanceManager;

    EntityFactory(GPUInstanceManager& instanceManager);
    EntityHandle createStaticMesh(entt::registry& reg, std::optional<MeshHandle> handle = std::nullopt);
    EntityHandle createCamera(entt::registry& reg);
};
}  // namespace batap
