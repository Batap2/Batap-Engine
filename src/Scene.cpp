#include "Scene.h"

#include "Systems/TransformSystem.h"

namespace rayvox
{
Scene::Scene(Context& ctx) : _ctx(ctx), _instanceManager(*ctx._gpuInstanceManager.get())
{
    _registry.ctx().emplace<TransformSystem>();
}
}  // namespace rayvox
