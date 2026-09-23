#include "VulkanPipelines.h"

#include <cstdint>
#include <fstream>
#include <stdexcept>

namespace batap
{

ShaderModule::ShaderModule(VkDevice device, const void* spirv, size_t sizeBytes) : device_(device)
{
    create(spirv, sizeBytes);
}

ShaderModule::ShaderModule(VkDevice device, const std::string& spvPath) : device_(device)
{
    std::ifstream file(spvPath, std::ios::binary | std::ios::ate);
    if (!file)
        throw std::runtime_error("ShaderModule : unfindable " + spvPath);

    std::vector<char> spirv(static_cast<size_t>(file.tellg()));
    file.seekg(0);
    file.read(spirv.data(), static_cast<std::streamsize>(spirv.size()));

    create(spirv.data(), spirv.size());
}

void ShaderModule::create(const void* spirv, size_t sizeBytes)
{
    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = sizeBytes;
    info.pCode = static_cast<const uint32_t*>(spirv);

    if (vkCreateShaderModule(device_, &info, nullptr, &module_) != VK_SUCCESS)
        throw std::runtime_error("ShaderModule : non valid SPIR-V");
}

ShaderModule::~ShaderModule()
{
    if (module_)
        vkDestroyShaderModule(device_, module_, nullptr);
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::shaders(VkShaderModule vs, VkShaderModule ps)
{
    vs_ = vs;
    ps_ = ps;
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::vertexAttribute(uint32_t location,
                                                                  VkFormat format,
                                                                  uint32_t stride)
{
    VkVertexInputBindingDescription binding{};
    binding.binding = location;
    binding.stride = stride;
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    vertexBindings_.push_back(binding);

    VkVertexInputAttributeDescription attribute{};
    attribute.location = location;
    attribute.binding = location;
    attribute.format = format;
    vertexAttributes_.push_back(attribute);
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::colorFormat(VkFormat format)
{
    colorFormat_ = format;
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::depth(VkFormat format, bool write,
                                                        VkCompareOp compare)
{
    depthFormat_ = format;
    depthWrite_ = write;
    depthCompare_ = compare;
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::cullBack()
{
    cullMode_ = VK_CULL_MODE_BACK_BIT;
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::topology(VkPrimitiveTopology topology)
{
    topology_ = topology;
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::depthOnly()
{
    depthOnly_ = true;
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::depthClamp()
{
    depthClamp_ = true;
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::dynamicDepthBias()
{
    dynamicDepthBias_ = true;
    return *this;
}

VkPipeline GraphicsPipelineBuilder::build(VkDevice device, VkPipelineLayout layout) const
{
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs_;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = ps_;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = static_cast<uint32_t>(vertexBindings_.size());
    vertexInput.pVertexBindingDescriptions = vertexBindings_.data();
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexAttributes_.size());
    vertexInput.pVertexAttributeDescriptions = vertexAttributes_.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = topology_;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = cullMode_;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;
    raster.depthClampEnable = depthClamp_ ? VK_TRUE : VK_FALSE;
    raster.depthBiasEnable = dynamicDepthBias_ ? VK_TRUE : VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = depthFormat_ != VK_FORMAT_UNDEFINED;
    depthStencil.depthWriteEnable = depthWrite_;
    depthStencil.depthCompareOp = depthCompare_;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = depthOnly_ ? 0u : 1u;
    blend.pAttachments = depthOnly_ ? nullptr : &blendAttachment;

    const VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
                                            VK_DYNAMIC_STATE_DEPTH_BIAS};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = dynamicDepthBias_ ? 3u : 2u;
    dynamic.pDynamicStates = dynamicStates;

    VkPipelineRenderingCreateInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = depthOnly_ ? 0u : 1u;
    rendering.pColorAttachmentFormats = depthOnly_ ? nullptr : &colorFormat_;
    rendering.depthAttachmentFormat = depthFormat_;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = &rendering;
    pipelineInfo.stageCount = depthOnly_ ? 1u : 2u;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &raster;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &blend;
    pipelineInfo.pDynamicState = &dynamic;
    pipelineInfo.layout = layout;

    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) !=
        VK_SUCCESS)
        throw std::runtime_error("GraphicsPipelineBuilder : vkCreateGraphicsPipelines");
    return pipeline;
}

void setViewportYUpRect(VkCommandBuffer cmd, uint32_t x, uint32_t y, uint32_t width,
                        uint32_t height)
{
    VkViewport viewport{};
    viewport.x = static_cast<float>(x);
    viewport.y = static_cast<float>(y + height);
    viewport.width = static_cast<float>(width);
    viewport.height = -static_cast<float>(height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{{static_cast<int32_t>(x), static_cast<int32_t>(y)}, {width, height}};
    vkCmdSetScissor(cmd, 0, 1, &scissor);
}

void setViewportYUp(VkCommandBuffer cmd, uint32_t width, uint32_t height)
{
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = static_cast<float>(height);
    viewport.width = static_cast<float>(width);
    viewport.height = -static_cast<float>(height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{{0, 0}, {width, height}};
    vkCmdSetScissor(cmd, 0, 1, &scissor);
}

}  // namespace batap
