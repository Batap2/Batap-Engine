#include "Renderer/SceneBinding.h"

#include "Engine.h"
#include "Renderer/Vulkan/VulkanRenderer.h"
#include "Renderer/Vulkan/VulkanScenePasses.h"
#include "World.h"

namespace batap
{
void bindScene(Engine& ctx, World& world)
{
    auto* passes = ctx.renderer_->scenePasses();
    ctx.renderer_->setSceneGraph(
        [&ctx, &world, passes](RenderGraph& graph, uint32_t frame, const RenderTargets& targets)
        { return passes->addPasses(graph, frame, targets, world.renderArgs(), ctx); });
}
}  // namespace batap
