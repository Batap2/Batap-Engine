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
    ctx.renderer_->setSceneRecord(
        [&ctx, &world, passes](VkCommandBuffer cmd, uint32_t frame,
                               const RenderTargets& targets)
        { return passes->record(cmd, frame, targets, world.renderArgs(), ctx); });
}
}  // namespace batap
