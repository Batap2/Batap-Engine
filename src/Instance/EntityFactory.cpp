#include "EntityFactory.h"
#include "Components/Camera_C.h"
#include "Components/EntityHandle.h"
#include "Components/Mesh_C.h"
#include "Components/RenderInstanceID_C.h"
#include "Components/Transform_C.h"
#include "Instance/InstanceKind.h"
#include "Instance/InstanceManager.h"

namespace batap
{
EntityFactory::EntityFactory(GPUInstanceManager& instanceManager)
    : _instanceManager(instanceManager)
{}

EntityHandle EntityFactory::createStaticMesh(entt::registry& reg, AssetHandle handle)
{
    auto entity = reg.create();
    reg.emplace<Mesh_C>(entity, handle);
    reg.emplace<Transform_C>(entity);
    EntityHandle h{&reg, entity};

    auto iid = _instanceManager._meshInstancesPool.assign(h);
    auto& rInstance = reg.emplace<RenderInstance_C>(entity);
    rInstance._instanceID = iid;
    rInstance._kind = InstanceKind::StaticMesh;

    return h;
}
EntityHandle EntityFactory::createCamera(entt::registry& reg)
{
    auto entity = reg.create();
    reg.emplace<Camera_C>(entity);
    reg.emplace<Transform_C>(entity);
    EntityHandle h{&reg, entity};

    auto iid = _instanceManager._cameraInstancesPool.assign(h);
    auto& rInstance = reg.emplace<RenderInstance_C>(entity);
    rInstance._instanceID = iid;
    rInstance._kind = InstanceKind::Camera;

    return h;
}
}  // namespace batap
