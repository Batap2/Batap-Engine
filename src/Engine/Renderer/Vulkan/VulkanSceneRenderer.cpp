// Implémentation Vulkan de SceneRenderer.h (le header est neutre, seul le
// .cpp est par backend). La logique des passes vit dans VulkanScenePasses ;
// ce fichier n'est que le pont entre le header neutre et le backend.

#include "Renderer/SceneRenderer.h"

#include "Instance/InstanceManager.h"
#include "Renderer/Vulkan/VulkanRenderer.h"
#include "Renderer/Vulkan/VulkanScenePasses.h"

namespace batap
{

void SceneRenderer::uploadDirty()
{
    args_.instanceManager_->uploadRemainingFrameDirty(ctx_);
}

void SceneRenderer::initRenderPasses()
{
    auto* passes = ctx_.renderer_->scenePasses();
    ctx_.renderer_->setSceneRecord(
        [this, passes](VkCommandBuffer cmd, uint32_t frame, uint32_t width, uint32_t height)
        { passes->record(cmd, frame, width, height, args_, ctx_); });
}

}  // namespace batap
