#include "Renderer/Vulkan/Passes/GeometryPass.h"

#include "Assets/Mesh.h"
#include "Renderer/Vulkan/Passes/MeshDraws.h"
#include "Renderer/Vulkan/VulkanContext.h"
#include "Renderer/Vulkan/VulkanPipelines.h"
#include "Renderer/Vulkan/VulkanResources.h"

namespace batap
{
GeometryPass::~GeometryPass()
{
    vkDestroyPipeline(setup_.ctx_.device_, pipeline_, nullptr);
    vkDestroyPipeline(setup_.ctx_.device_, rtPipeline_, nullptr);
}

void GeometryPass::buildPipelines(const ShaderModules& modules)
{
    vkDestroyPipeline(setup_.ctx_.device_, pipeline_, nullptr);
    vkDestroyPipeline(setup_.ctx_.device_, rtPipeline_, nullptr);

    const auto build = [&](VkShaderModule pixelShader, VkPipelineLayout layout)
    {
        return GraphicsPipelineBuilder()
            .shaders(modules[GeometryVS], pixelShader)
            // Locations follow Mesh::Stream — changing one means
            // changing the other.
            .vertexAttribute(Mesh::Position, VK_FORMAT_R32G32B32_SFLOAT, 12)
            .vertexAttribute(Mesh::Normal, VK_FORMAT_R32G32B32_SFLOAT, 12)
            .vertexAttribute(Mesh::UV0, VK_FORMAT_R32G32_SFLOAT, 8)
            .vertexAttribute(Mesh::Tangent, VK_FORMAT_R32G32B32A32_SFLOAT, 16)
            .colorFormat(setup_.colorFormat_)
            .depth(setup_.depthFormat_, true, VK_COMPARE_OP_LESS)
            .cullBack()
            .build(setup_.ctx_.device_, layout);
    };
    pipeline_ = build(modules[GeometryPS], setup_.layout_);
    rtPipeline_ = modules[GeometryRtPS] ? build(modules[GeometryRtPS], setup_.rtLayout_)
                                        : VK_NULL_HANDLE;
}

void GeometryPass::record(const PassContext& pass)
{
    vkCmdBindPipeline(pass.cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      pass.rayTracing_ ? rtPipeline_ : pipeline_);
    recordMeshDraws(pass, setup_.resources_, Mesh::VertexStreams);
}
}  // namespace batap
