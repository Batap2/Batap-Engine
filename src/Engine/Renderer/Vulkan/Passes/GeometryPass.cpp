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
}

void GeometryPass::buildPipelines(const ShaderModules& modules)
{
    if (pipeline_)
        vkDestroyPipeline(setup_.ctx_.device_, pipeline_, nullptr);

    pipeline_ = GraphicsPipelineBuilder()
                    .shaders(modules[GeometryVS], modules[GeometryPS])
                    // Locations follow Mesh::Stream — changing one means
                    // changing the other.
                    .vertexAttribute(Mesh::Position, VK_FORMAT_R32G32B32_SFLOAT, 12)
                    .vertexAttribute(Mesh::Normal, VK_FORMAT_R32G32B32_SFLOAT, 12)
                    .vertexAttribute(Mesh::UV0, VK_FORMAT_R32G32_SFLOAT, 8)
                    .vertexAttribute(Mesh::Tangent, VK_FORMAT_R32G32B32A32_SFLOAT, 16)
                    .colorFormat(setup_.colorFormat_)
                    .depth(setup_.depthFormat_, true, VK_COMPARE_OP_LESS)
                    .cullBack()
                    .build(setup_.ctx_.device_, setup_.layout_);
}

void GeometryPass::record(const PassContext& pass)
{
    vkCmdBindPipeline(pass.cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    recordMeshDraws(pass, setup_.resources_, Mesh::VertexStreams);
}
}  // namespace batap
