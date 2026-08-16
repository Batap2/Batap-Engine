#include "Systems.h"

#include "Engine.h"
#include "FreeCamController_S.h"
#include "Scene.h"
#include "Transform_S.h"
#include "World.h"

#include <memory>

namespace batap
{

Systems::~Systems() = default;

void Systems::update(float deltaTime, Engine& ctx, World& world)
{
    freecam_->update(ctx, world, deltaTime);
    transforms_->update(world.scene_->registry_, *world.instanceManager_);
}

Systems::Systems()
{
    freecam_ = std::make_unique<FreeCamController_S>();
    transforms_ = std::make_unique<Transform_S>();
}
}  // namespace batap
