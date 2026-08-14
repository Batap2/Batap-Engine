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
    args_.instanceManager_->uploadRemainingFrameDirty(_ctx);
}

void SceneRenderer::initRenderPasses()
{
    auto* passes = _ctx._renderer->scenePasses();
    _ctx._renderer->setSceneRecord(
        [this, passes](VkCommandBuffer cmd, uint32_t frame, uint32_t width, uint32_t height)
        { passes->record(cmd, frame, width, height, args_, _ctx); });
}

}  // namespace batap
