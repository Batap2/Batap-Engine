#include "World.h"
#include <memory>

#include "Components/Camera_C.h"
#include "Components/ComponentFlag.h"
#include "Context.h"
#include "Instance/EntityFactory.h"
#include "Instance/InstanceManager.h"
#include "Renderer/Renderer.h"
#include "Systems/Systems.h"

namespace batap
{
World::World(Context& ctx)
{
    systems_ = std::make_unique<Systems>();
    instanceManager_ = std::make_unique<GPUInstanceManager>(ctx);
    entityFactory_ = std::make_unique<EntityFactory>(*instanceManager_);

    // refresh camera ratio on window resize
    ctx._renderer->onResize(
        [this](uint32_t, uint32_t)
        {
            if (!scene_)
                return;
            auto& reg = scene_->_registry;
            reg.view<Camera_C>().each(
                [&](entt::entity e, Camera_C& c)
                { instanceManager_->markDirty({&reg, e}, ComponentFlag::Camera); });
        });
}

World::~World() = default;

void World::update(Context& ctx)
{
    scene_->update(ctx._deltaTime, ctx, *this);
    systems_->update(ctx._deltaTime, ctx, *this);
}
}  // namespace batap
