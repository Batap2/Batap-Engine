#include "Systems.h"

#include "Engine.h"
#include "FreeCamController_S.h"
#include "Billboard_S.h"
#include "Bounds_S.h"
#include "Physics_S.h"
#include "Transform_S.h"
#include "World.h"

#include <memory>

namespace batap
{

Systems::~Systems() = default;

void Systems::update(float deltaTime, Engine& ctx, World& world)
{
    freecam_->update(ctx, world, deltaTime);
    if (transforms_->update(world.registry_, world.instances()))
        world.markSpatialDirty();
    physics_->drawColliders(world);
    bounds_->drawBounds(world, ctx);
    billboards_->submit(world, ctx);
}

Systems::Systems()
{
    freecam_ = std::make_unique<FreeCamController_S>();
    transforms_ = std::make_unique<Transform_S>();
    physics_ = std::make_unique<Physics_S>();
    billboards_ = std::make_unique<Billboard_S>();
    bounds_ = std::make_unique<Bounds_S>();
}
}  // namespace batap
