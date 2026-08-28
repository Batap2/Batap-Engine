#pragma once

#include <volk.h>

#include "Renderer/SceneRenderer.h"  // SceneRenderArgs
#include "Renderer/Vulkan/VulkanShaderCompiler.h"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace batap
{
struct VulkanContext;
struct ResourceManager;

// Les passes de scène (geometry + sky), enregistrées dans le rendering scope
// ouvert par Renderer::render(). Modèle de binding (docs/vulkan.md §10) :
//   set 0 = bindless global (ResourceManager : sampler + textures) ;
//   set 1 = les 5 storage buffers de la frame (caméras, instances, lights,
//           matériaux, skybox) — un set par frame en vol, réécrit chaque
//           frame car les buffers des pools changent quand ils grandissent ;
//   push constants = {camIdx, instanceIdx, nLights}, partagés VS/PS.
// Un seul pipeline layout pour les deux passes.
struct ScenePasses
{
    void init(VulkanContext& ctx, ResourceManager& resources, VkFormat colorFormat,
              VkFormat depthFormat);
    void shutdown(VkDevice device);

    // À appeler entre vkCmdBeginRendering / EndRendering
    void record(VkCommandBuffer cmd, uint32_t frame, uint32_t width, uint32_t height,
                const SceneRenderArgs& args, Engine& ctx);

    // Hot reload : si un .hlsl du dossier source a changé, recompile via
    // libdxcompiler et reconstruit les pipelines (attend l'idle GPU).
    // En cas d'erreur HLSL, log et garde les pipelines courantes.
    // À appeler hors enregistrement de command buffer.
    void checkHotReload(VkDevice device);

   private:
    void writeFrameSet(uint32_t frame, const SceneRenderArgs& args, Engine& ctx);
    // Construit (ou reconstruit) les deux pipelines. Les modules restent la
    // propriété de l'appelant : build() ne fait que les lire.
    void buildPipelines(VkDevice device, VkShaderModule vs, VkShaderModule ps,
                        VkShaderModule skyVS, VkShaderModule skyPS);

    ResourceManager* resources_ = nullptr;
    VkFormat colorFormat_ = VK_FORMAT_UNDEFINED;
    VkFormat depthFormat_ = VK_FORMAT_UNDEFINED;

    VkDescriptorSetLayout frameSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool framePool_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> frameSets_;

    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline geometryPipeline_ = VK_NULL_HANDLE;
    VkPipeline skyPipeline_ = VK_NULL_HANDLE;

    ShaderCompiler shaderCompiler_;
    std::filesystem::file_time_type shadersMtime_{};
};
}  // namespace batap
