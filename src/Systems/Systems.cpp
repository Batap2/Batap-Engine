#include "Systems.h"
#include "TransformSystem.h"

#include <memory>

namespace batap
{

Systems::~Systems() = default;

void Systems::update(float deltaTime, entt::registry& reg)
{
    _transforms->update(reg, _ctx);
}

Systems::Systems(Context& ctx) : _ctx(ctx)
{
    _transforms = std::make_unique<TransformSystem>();
}
}  // namespace batap
