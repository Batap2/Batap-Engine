#include "App.h"
#include <memory>

#include "Context.h"
#include "TestScene.h"
#include "Renderer/SceneRenderer.h"

namespace batap
{

void App::start(Context& ctx) {
    world_ = std::make_unique<World>(ctx);
    world_->scene_ = std::make_unique<TestScene>(*world_);
}

void App::update(Context& ctx)
{
    ctx.beginFrame();
    world_->update(ctx);
    ctx._sceneRenderer->setScene({&world_->scene_.get()->_registry, world_->instanceManager_.get()});
    ctx._sceneRenderer->uploadDirty();
    ctx.endFrame();
}

void App::shutdown(Context& ctx) {}

}  // namespace batap
