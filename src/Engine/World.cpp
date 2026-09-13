#include "World.h"
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <memory>

#include "Components/Camera_C.h"
#include "Components/Hierarchy_C.h"
#include "Engine.h"
#include "Game.h"
#include "InputManager.h"
#include "Instance/EntityFactory.h"
#include "Instance/InstanceManager.h"
#include "Physics/PhysicsWorld.h"
#include "Renderer/Renderer.h"
#include "Renderer/SceneBinding.h"
#include "Serialization/EntitySerializer.h"
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

    registry_.ctx().emplace<World*>(this);
    instanceManager_->connectHooks(registry_);
    systems_->physics_->connectHooks(registry_);

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

SceneRenderArgs World::renderArgs()
{
    return {&registry_, instanceManager_.get()};
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
