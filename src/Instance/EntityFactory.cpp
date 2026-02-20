#include "EntityFactory.h"
#include <optional>
#include "Components/Camera_C.h"
#include "Components/EntityHandle.h"
#include "Components/Mesh_C.h"
#include "Components/RenderInstanceID_C.h"
#include "Components/Transform_C.h"
#include "Components/Name_C.h"
#include "Handles.h"
#include "Instance/InstanceKind.h"
#include "Instance/InstanceManager.h"

namespace batap
{
EntityFactory::EntityFactory(GPUInstanceManager& instanceManager)
    : _instanceManager(instanceManager)
{}

EntityHandle EntityFactory::createStaticMesh(entt::registry& reg, std::optional<AssetHandle> handle)
{
    auto entity = reg.create();

    auto& nameC = reg.emplace<Name_C>(entity, "Static Mesh");

    auto& meshC = reg.emplace<Mesh_C>(entity);
    if(handle){
        meshC._mesh = *handle;
    }
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

    auto& nameC = reg.emplace<Name_C>(entity, "Camera");

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
