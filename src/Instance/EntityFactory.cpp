#pragma once

#include "EntityFactory.h"
#include "Components/Camera_C.h"
#include "Components/Mesh_C.h"
#include "Components/Transform_C.h"
#include "Instance/InstanceManager.h"

namespace rayvox
{
EntityFactory::EntityFactory(GPUInstanceManager& instanceManager)
    : _instanceManager(instanceManager)
{}

EntityHandle EntityFactory::createStaticMesh(entt::registry& reg, AssetHandle handle)
{
    auto entity = reg.create();
    reg.emplace<Mesh_C>(entity, handle);
    reg.emplace<Transform_C>(entity);
    _instanceManager._meshInstancesPool.assign({entity, &reg});
}
EntityHandle EntityFactory::createCamera(entt::registry& reg)
{
    auto entity = reg.create();
    reg.emplace<Camera_C>(entity);
    reg.emplace<Transform_C>(entity);
    _instanceManager._cameraInstancesPool.assign({entity, &reg});
}
}  // namespace rayvox
