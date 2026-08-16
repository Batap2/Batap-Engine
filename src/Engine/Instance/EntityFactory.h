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
    GPUInstanceManager& instanceManager_;

    EntityFactory(GPUInstanceManager& instanceManager);
    EntityHandle createEmpty(entt::registry& reg);
    EntityHandle createStaticMesh(entt::registry& reg, std::optional<MeshHandle> handle = std::nullopt);
    EntityHandle createCamera(entt::registry& reg);
    EntityHandle createPointLight(entt::registry& reg);
    EntityHandle createSkybox(entt::registry& reg);
    void destroy(EntityHandle h);
};
}  // namespace batap
