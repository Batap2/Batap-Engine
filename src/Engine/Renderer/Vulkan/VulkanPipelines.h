#pragma once

#include <volk.h>

#include <cstdint>
#include <string>
#include <vector>

namespace batap
{
struct ShaderModule
{
    ShaderModule(VkDevice device, const std::string& spvPath);
    ShaderModule(VkDevice device, const void* spirv, size_t sizeBytes);
    ~ShaderModule();

    ShaderModule(const ShaderModule&) = delete;
    ShaderModule& operator=(const ShaderModule&) = delete;

    operator VkShaderModule() const { return module_; }

   private:
    void create(const void* spirv, size_t sizeBytes);

    VkDevice device_ = VK_NULL_HANDLE;
    VkShaderModule module_ = VK_NULL_HANDLE;
};

struct GraphicsPipelineBuilder
{
    GraphicsPipelineBuilder& shaders(VkShaderModule vs, VkShaderModule ps);
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

void setViewportYUp(VkCommandBuffer cmd, uint32_t width, uint32_t height);
}  // namespace batap
