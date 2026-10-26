#include "Renderer/SceneBinding.h"

#include "Engine.h"
#include "Renderer/Renderer.h"
#include "Renderer/Scene/SceneFrame.h"
#include "World.h"

namespace batap
{
void bindScene(Engine& ctx, World& world)
{
    SceneFrame* scene = ctx.renderer_->sceneFrame();
    ctx.renderer_->setSceneGraph(
        [&ctx, &world, scene](RenderGraph& graph, uint32_t frame, const RenderTargets& targets,
                              GraphBuffer uploads)
        { return scene->addPasses(graph, frame, targets, uploads, world.renderArgs(), ctx); });
}
}  // namespace batap
