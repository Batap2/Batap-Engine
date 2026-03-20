#include "EntityFactory.h"
#include <optional>
#include "Assets/AssetHandle.h"
#include "Components/Camera_C.h"
#include "Components/EntityHandle.h"
#include "Components/Mesh_C.h"
#include "Components/Name_C.h"
#include "Components/PointLight_C.h"
#include "Components/RenderInstanceID_C.h"
#include "Components/Transform_C.h"
#include "Handles.h"
#include "Instance/InstanceKind.h"
#include "Instance/InstanceManager.h"
#include "Systems/Hierarchy_S.h"

namespace batap
{
EntityFactory::EntityFactory(GPUInstanceManager& instanceManager)
    : _instanceManager(instanceManager)
{}

EntityHandle EntityFactory::createStaticMesh(entt::registry& reg, std::optional<MeshHandle> handle)
{
    auto entity = reg.create();

    auto& nameC = reg.emplace<Name_C>(entity, "Static Mesh");

    auto& meshC = reg.emplace<Mesh_C>(entity);
    if (handle)
    {
        meshC._mesh = *handle;
    }
    reg.emplace<Transform_C>(entity);
    EntityHandle h{&reg, entity};

    auto iid = _instanceManager._meshInstancesPool.insert(h);

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

    auto iid = _instanceManager._cameraInstancesPool.insert(h);
    auto& rInstance = reg.emplace<RenderInstance_C>(entity);
    rInstance._instanceID = iid;
    rInstance._kind = InstanceKind::Camera;

    return h;
}

EntityHandle EntityFactory::createPointLight(entt::registry& reg)
{
    auto entity = reg.create();

    auto& nameC = reg.emplace<Name_C>(entity, "PointLight");

    reg.emplace<PointLight_C>(entity);
    reg.emplace<Transform_C>(entity);
    EntityHandle h{&reg, entity};

    auto iid = _instanceManager.pointLightInstancePool_.insert(h);
    auto& rInstance = reg.emplace<RenderInstance_C>(entity);
    rInstance._instanceID = iid;
    rInstance._kind = InstanceKind::PointLight;

    return h;
}

void EntityFactory::destroy(EntityHandle h)
{
    auto& reg = *h._reg;
    if (!reg.valid(h._entity))
        return;

    // Collect children before modifying hierarchy
    std::vector<entt::entity> childList;
    for (entt::entity child : Hierarchy_S::children(h))
        childList.push_back(child);

    for (auto child : childList)
        destroy({&reg, child});

    Hierarchy_S::detach(h);

    if (auto* ri = reg.try_get<RenderInstance_C>(h._entity))
    {
        switch (ri->_kind)
        {
        case InstanceKind::StaticMesh:
            _instanceManager._meshInstancesPool.remove(h);
            break;
        case InstanceKind::Camera:
            _instanceManager._cameraInstancesPool.remove(h);
            break;
        case InstanceKind::PointLight:
            _instanceManager.pointLightInstancePool_.remove(h);
            break;
        }
    }

    reg.destroy(h._entity);
}
}  // namespace batap
