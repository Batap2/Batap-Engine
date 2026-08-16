#include "EntityFactory.h"
#include <optional>
#include "Assets/AssetHandle.h"
#include "Components/Camera_C.h"
#include "Components/EntityHandle.h"
#include "Components/Kind_C.h"
#include "Components/Mesh_C.h"
#include "Components/Name_C.h"
#include "Components/PointLight_C.h"
#include "Components/Skybox_C.h"
#include "Components/RenderInstanceID_C.h"
#include "Components/Transform_C.h"
#include "EntityKind.h"
#include "Handles.h"
#include "Instance/InstanceManager.h"
#include "Systems/Hierarchy_S.h"


namespace batap
{
EntityFactory::EntityFactory(GPUInstanceManager& instanceManager)
    : instanceManager_(instanceManager)
{}

EntityHandle EntityFactory::createEmpty(entt::registry& reg)
{
    auto entity = reg.create();
    reg.emplace<Name_C>(entity, "Entity");
    reg.emplace<Kind_C>(entity, EntityKind::Empty);
    return {&reg, entity};
}

EntityHandle EntityFactory::createStaticMesh(entt::registry& reg, std::optional<MeshHandle> handle)
{
    auto entity = reg.create();

    auto& nameC = reg.emplace<Name_C>(entity, "Static Mesh");

    auto& meshC = reg.emplace<Mesh_C>(entity);
    if (handle)
    {
        meshC.mesh_ = *handle;
    }
    reg.emplace<Transform_C>(entity);
    EntityHandle h{&reg, entity};

    auto iid = instanceManager_.pool<StaticMeshInstance>().insert(h);

    reg.emplace<Kind_C>(entity, EntityKind::StaticMesh);
    auto& rInstance = reg.emplace<RenderInstance_C>(entity);
    rInstance.instanceID_ = iid;

    return h;
}
EntityHandle EntityFactory::createCamera(entt::registry& reg)
{
    auto entity = reg.create();

    auto& nameC = reg.emplace<Name_C>(entity, "Camera");

    reg.emplace<Camera_C>(entity);
    reg.emplace<Transform_C>(entity);
    EntityHandle h{&reg, entity};

    auto iid = instanceManager_.pool<CameraInstance>().insert(h);
    reg.emplace<Kind_C>(entity, EntityKind::Camera);
    auto& rInstance = reg.emplace<RenderInstance_C>(entity);
    rInstance.instanceID_ = iid;

    return h;
}

EntityHandle EntityFactory::createPointLight(entt::registry& reg)
{
    auto entity = reg.create();

    auto& nameC = reg.emplace<Name_C>(entity, "PointLight");

    reg.emplace<PointLight_C>(entity);
    reg.emplace<Transform_C>(entity);
    EntityHandle h{&reg, entity};

    auto iid = instanceManager_.pool<PointLightInstance>().insert(h);
    reg.emplace<Kind_C>(entity, EntityKind::PointLight);
    auto& rInstance = reg.emplace<RenderInstance_C>(entity);
    rInstance.instanceID_ = iid;

    return h;
}

EntityHandle EntityFactory::createSkybox(entt::registry& reg)
{
    auto entity = reg.create();
    reg.emplace<Name_C>(entity, "Skybox");
    reg.emplace<Skybox_C>(entity);
    reg.emplace<Kind_C>(entity, EntityKind::Skybox);
    EntityHandle h{&reg, entity};

    auto iid = instanceManager_.pool<SkyboxInstance>().insert(h);
    auto& rInstance = reg.emplace<RenderInstance_C>(entity);
    rInstance.instanceID_ = iid;

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
