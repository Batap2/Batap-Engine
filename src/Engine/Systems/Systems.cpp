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
    _transforms->update(world.scene_->_registry, *world.instanceManager_);
}

Systems::Systems()
{
    freecam_ = std::make_unique<FreeCamController_S>();
    _transforms = std::make_unique<Transform_S>();
}
}  // namespace batap
