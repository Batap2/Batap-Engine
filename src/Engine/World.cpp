#include "World.h"
#include <memory>

#include "Context.h"
#include "Instance/EntityFactory.h"
#include "Instance/InstanceManager.h"
#include "Systems/Systems.h"


namespace batap
{
World::World(Context& ctx)
{
    systems_ = std::make_unique<Systems>();
    instanceManager_ = std::make_unique<GPUInstanceManager>(ctx);
    entityFactory_ = std::make_unique<EntityFactory>(*instanceManager_);
}

World::~World() = default;

void World::update(Context& ctx)
{
    scene_->update(ctx._deltaTime, ctx, *this);
    systems_->update(ctx._deltaTime, ctx, *this);
}
}  // namespace batap
