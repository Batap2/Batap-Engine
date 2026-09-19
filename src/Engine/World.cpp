#include "World.h"
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <memory>

#include "Components/Camera_C.h"
#include "Components/Hierarchy_C.h"
#include "Components/Transform_C.h"
#include "Engine.h"
#include "Game.h"
#include "InputManager.h"
#include "Instance/EntityFactory.h"
#include "Instance/InstanceManager.h"
#include "Physics/PhysicsWorld.h"

#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/RayCast.h>

#include "Physics/JoltConvert.h"
#include "Renderer/Renderer.h"
#include "Renderer/SceneBinding.h"
#include "Serialization/EntitySerializer.h"
#include "Spatial/SpatialIndex.h"
#include "Systems/Camera_S.h"
#include "Systems/Character_S.h"
#include "Systems/Physics_S.h"
#include "Systems/Systems.h"
#include "Systems/Transform_S.h"

namespace batap
{
World::World(Engine& ctx) : ctx_(&ctx)
{
    systems_ = std::make_unique<Systems>();
    physics_ = std::make_unique<PhysicsWorld>();
    instanceManager_ = std::make_unique<GPUInstanceManager>(ctx);
    entityFactory_ = std::make_unique<EntityFactory>();
    spatialIndex_ = std::make_unique<SpatialIndex>();

    registry_.ctx().emplace<World*>(this);
    instanceManager_->connectHooks(registry_);
    systems_->physics_->connectHooks(registry_);
    systems_->characters_->connectHooks(registry_);
    systems_->cameras_->connectHooks(registry_);
    spatialIndex_->connectHooks(registry_);

    // refresh camera ratio on window resize
    ctx.renderer_->onResize(
        [this](uint32_t, uint32_t)
        {
            registry_.view<Camera_C>().each(
                [&](entt::entity e, Camera_C& c)
                { instanceManager_->markDirty<Camera_C>({&registry_, e}); });
        });

    bindScene(ctx, *this);
}

World::~World() = default;

void World::markSpatialDirty()
{
    spatialIndex_->markDirty();
}

SpatialIndex& World::spatialIndex()
{
    spatialIndex_->refresh(*this, *ctx_);
    return *spatialIndex_;
}

SceneRenderArgs World::renderArgs()
{
    return {&registry_, instanceManager_.get(), renderCamera()};
}

entt::entity World::renderCamera()
{
    if (registry_.valid(renderCamera_) && registry_.all_of<Camera_C, Transform_C>(renderCamera_))
        return renderCamera_;
    for (entt::entity e : registry_.view<Camera_C, Transform_C>())
        if (registry_.get<Camera_C>(e).active_)
            return e;
    return entt::null;
}

const std::vector<ContactEvent>& World::contacts() const
{
    return systems_->physics_->contacts();
}

RayHit World::raycastPhysics(const Ray& ray) const
{
    const float from = ray.tMin_;
    const float to = std::min(ray.tMax_, 1e7f);
    if (to <= from)
        return {};

    const JPH::RRayCast cast{toJolt(ray.origin_ + ray.dir_ * from), toJolt(ray.dir_ * (to - from))};
    JPH::RayCastResult result;
    if (!physics_->system().GetNarrowPhaseQuery().CastRay(cast, result))
        return {};

    const uint64_t user = physics_->bodies().GetUserData(result.mBodyID);
    if (user == 0)
        return {};

    RayHit hit;
    hit.entity_ = static_cast<entt::entity>(user - 1u);
    hit.t_ = from + result.mFraction * (to - from);
    hit.point_ = ray.origin_ + ray.dir_ * hit.t_;

    JPH::BodyLockRead lock(physics_->system().GetBodyLockInterface(), result.mBodyID);
    if (lock.Succeeded())
        hit.normal_ = toEigen(
            lock.GetBody().GetWorldSpaceSurfaceNormal(result.mSubShapeID2, toJolt(hit.point_)));
    return hit;
}

void World::setGravity(const v3f& g)
{
    physics_->system().SetGravity(toJolt(g));
}

v3f World::gravity() const
{
    return toEigen(physics_->system().GetGravity());
}

void World::update()
{
    systems_->update(ctx_->deltaTime_, *ctx_, *this);
    instanceManager_->uploadRemainingFrameDirty(*ctx_);
}

void World::update(Game& game)
{
    const float dt = time_.paused_ ? 0.f : ctx_->deltaTime_ * time_.scale_;
    time_.accumulator_ += std::min(dt, 0.25f);
    while (time_.accumulator_ >= time_.fixedDt_)
    {
        game.fixedUpdate(*this, time_.fixedDt_);
        systems_->physics_->fixedUpdate(*this, time_.fixedDt_);
        systems_->characters_->fixedUpdate(*this, time_.fixedDt_);
        time_.accumulator_ -= time_.fixedDt_;
    }

    game.update(*this, dt);
    systems_->update(ctx_->deltaTime_, *ctx_, *this);
    game.lateUpdate(*this, dt);
    systems_->transforms_->update(registry_, *instanceManager_);
    instanceManager_->uploadRemainingFrameDirty(*ctx_);
}

InputManager& World::input()
{
    return *ctx_->inputManager_;
}

DebugDraw& World::debug()
{
    return ctx_->debug();
}

DebugDraw& World::debugOverlay()
{
    return ctx_->debugOverlay();
}

Billboards& World::billboards()
{
    return ctx_->billboards();
}

void World::resetScene()
{
    auto& reg = registry_;

    std::vector<entt::entity> roots;
    for (auto e : reg.storage<entt::entity>())
    {
        if (!reg.valid(e))
            continue;
        auto* hc = reg.try_get<Hierarchy_C>(e);
        if (!hc || hc->parent == entt::null)
            roots.push_back(e);
    }
    for (auto e : roots)
        entityFactory_->destroy({&reg, e});

    reg = entt::registry{};
    reg.ctx().emplace<World*>(this);
    instanceManager_->connectHooks(reg);
    systems_->physics_->connectHooks(reg);
    systems_->characters_->connectHooks(reg);
    systems_->cameras_->connectHooks(reg);
    spatialIndex_->connectHooks(reg);
    spatialIndex_->markDirty();

    physics_->clear();
}

bool World::loadScene(const std::string& path)
{
    namespace fs = std::filesystem;

    // Asset paths inside a .btpl are relative to the project dir; guessing a
    // base from the scene file's location resolves them wrong as soon as the
    // scene lives in a subfolder.
    const std::string& base = ctx_->assetManager_->baseDir();
    if (base.empty())
    {
        std::cerr << "[World] loadScene: call Engine::setProjectDir() first.\n";
        return false;
    }

    fs::path scenePath{path};
    if (scenePath.is_relative())
        scenePath = fs::path(base) / scenePath;

    if (!fs::exists(scenePath))
    {
        std::cerr << "[World] loadScene: file not found: " << scenePath.string() << "\n";
        return false;
    }

    EntitySerializer::clearSceneAndLoad(*this, *ctx_, scenePath.string());
    return true;
}
}  // namespace batap
