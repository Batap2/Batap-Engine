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
        [&ctx, &world, passes](VkCommandBuffer cmd, uint32_t frame, uint32_t width,
                               uint32_t height)
        { passes->record(cmd, frame, width, height, world.renderArgs(), ctx); });
}
}  // namespace batap
