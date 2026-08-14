#pragma once

#include <volk.h>

#include <cstdint>
#include <string>
#include <vector>

namespace batap
{
// Chargement SPIR-V + construction de pipelines graphiques.
// Conventions du moteur, appliquées par le builder :
//  - dynamic rendering (les formats d'attachments remplacent le VkRenderPass) ;
//  - viewport/scissor dynamiques, posés au record avec un viewport à hauteur
//    NÉGATIVE → repère Y-up identique à DX12, shaders et matrices inchangés.

VkShaderModule loadShaderModule(VkDevice device, const std::string& spvPath);
VkShaderModule createShaderModule(VkDevice device, const void* spirv, size_t sizeBytes);

struct GraphicsPipelineBuilder
{
    GraphicsPipelineBuilder& shaders(VkShaderModule vs, VkShaderModule ps);
    // Un binding de vertex buffer par attribut (le layout des meshes du
    // moteur : position, normal, uv, tangent dans des buffers séparés)
    GraphicsPipelineBuilder& vertexAttribute(uint32_t location, VkFormat format, uint32_t stride);
    GraphicsPipelineBuilder& colorFormat(VkFormat format);
    GraphicsPipelineBuilder& depth(VkFormat format, bool write, VkCompareOp compare);
    GraphicsPipelineBuilder& cullBack();

    VkPipeline build(VkDevice device, VkPipelineLayout layout) const;

   private:
    VkShaderModule vs_ = VK_NULL_HANDLE;
    VkShaderModule ps_ = VK_NULL_HANDLE;
    std::vector<VkVertexInputBindingDescription> vertexBindings_;
    std::vector<VkVertexInputAttributeDescription> vertexAttributes_;
    VkFormat colorFormat_ = VK_FORMAT_UNDEFINED;
    VkFormat depthFormat_ = VK_FORMAT_UNDEFINED;
    bool depthWrite_ = false;
    VkCompareOp depthCompare_ = VK_COMPARE_OP_ALWAYS;
    VkCullModeFlags cullMode_ = VK_CULL_MODE_NONE;
};

// Viewport Y-up (hauteur négative) + scissor, pour les états dynamiques
void setViewportYUp(VkCommandBuffer cmd, uint32_t width, uint32_t height);
}  // namespace batap
