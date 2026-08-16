#include "World.h"
#include <filesystem>
#include <iostream>
#include <memory>

#include "Components/Camera_C.h"
#include "Components/ComponentFlag.h"
#include "Engine.h"
#include "Instance/EntityFactory.h"
#include "Instance/InstanceManager.h"
#include "Renderer/Renderer.h"
#include "Renderer/SceneRenderer.h"
#include "Serialization/EntitySerializer.h"
#include "Systems/Systems.h"

namespace batap
{
World::World(Engine& ctx) : ctx_(&ctx)
{
    systems_ = std::make_unique<Systems>();
    instanceManager_ = std::make_unique<GPUInstanceManager>(ctx);
    entityFactory_ = std::make_unique<EntityFactory>(*instanceManager_);

    // Empty scene, never a null one. The editor replaces it with its own subclass.
    scene_ = std::make_unique<Scene>(*instanceManager_);

    // refresh camera ratio on window resize
    ctx.renderer_->onResize(
        [this](uint32_t, uint32_t)
        {
            if (!scene_)
                return;
            auto& reg = scene_->registry_;
            reg.view<Camera_C>().each(
                [&](entt::entity e, Camera_C& c)
                { instanceManager_->markDirty({&reg, e}, ComponentFlag::Camera); });
        });
}

World::~World() = default;

void World::update()
{
    scene_->update(ctx_->deltaTime_, *ctx_, *this);
    systems_->update(ctx_->deltaTime_, *ctx_, *this);
    ctx_->sceneRenderer_->setScene({&scene_->registry_, instanceManager_.get()});
    ctx_->sceneRenderer_->uploadDirty();
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
