#include "Renderer/Draws/MeshDrawer.h"

#include "Assets/AssetManager.h"
#include "Assets/Mesh.h"
#include "Components/Mesh_C.h"
#include "Instance/InstanceManager.h"
#include "Renderer/Device/Formats.h"
#include "Renderer/Device/Pipelines.h"
#include "Renderer/Device/ResourceManager.h"
#include "Renderer/Device/VulkanContext.h"

#include <array>

namespace batap
{
MeshDrawer::~MeshDrawer()
{
    vkDestroyPipeline(setup_.ctx_.device_, prepassPipeline_, nullptr);
    vkDestroyPipeline(setup_.ctx_.device_, pipeline_, nullptr);
    vkDestroyPipeline(setup_.ctx_.device_, rtPipeline_, nullptr);
}

void MeshDrawer::buildPipelines(const ShaderModules& modules)
{
    vkDestroyPipeline(setup_.ctx_.device_, prepassPipeline_, nullptr);
    vkDestroyPipeline(setup_.ctx_.device_, pipeline_, nullptr);
    vkDestroyPipeline(setup_.ctx_.device_, rtPipeline_, nullptr);

    const auto geometry = [&](VkShaderModule pixelShader)
    {
        return GraphicsPipelineBuilder()
            .shaders(modules[GeometryVS], pixelShader)
            .vertexAttribute(Mesh::Position, VK_FORMAT_R32G32B32_SFLOAT, 12)
            .vertexAttribute(Mesh::Normal, VK_FORMAT_R32G32B32_SFLOAT, 12)
            .vertexAttribute(Mesh::UV0, VK_FORMAT_R32G32_SFLOAT, 8)
            .vertexAttribute(Mesh::Tangent, VK_FORMAT_R32G32B32A32_SFLOAT, 16)
            .cullBack();
    };
    prepassPipeline_ = geometry(modules[PrepassPS])
                           .colorFormat(ViewNormalFormat)
                           .depth(setup_.depthFormat_, true, VK_COMPARE_OP_GREATER)
                           .build(setup_.ctx_.device_, setup_.layout_);
    const auto lit = [&](VkShaderModule pixelShader, VkPipelineLayout layout)
    {
        return geometry(pixelShader)
            .colorFormat(setup_.colorFormat_)
            .depth(setup_.depthFormat_, false, VK_COMPARE_OP_EQUAL)
            .build(setup_.ctx_.device_, layout);
    };
    pipeline_ = lit(modules[GeometryPS], setup_.layout_);
    rtPipeline_ = modules[GeometryRtPS] ? lit(modules[GeometryRtPS], setup_.rtLayout_)
                                        : VK_NULL_HANDLE;
}

void MeshDrawer::recordPrepass(const DrawContext& draw)
{
    vkCmdBindPipeline(draw.cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, prepassPipeline_);
    recordMeshDraws(draw, setup_.resources_, Mesh::VertexStreams, false);
}

void MeshDrawer::record(const DrawContext& draw)
{
    vkCmdBindPipeline(draw.cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      draw.rayTracing_ ? rtPipeline_ : pipeline_);
    recordMeshDraws(draw, setup_.resources_, Mesh::VertexStreams, false);
}

void recordMeshDraws(const DrawContext& draw, ResourceManager& resources, uint32_t streams,
                     bool castersOnly)
{
    DrawPush push = draw.push_;
    draw.reg_->view<Mesh_C>().each(
        [&](entt::entity e, Mesh_C& meshC)
        {
            if (!meshC.mesh_ || !meshC.visible_ || (castersOnly && !meshC.castShadows_))
                return;
            auto* mesh = draw.assetManager_->get(meshC.mesh_);
            if (!mesh)
                return;

            const auto id =
                draw.instanceManager_->pool<StaticMeshInstance>().getGPUIndex({draw.reg_, e});
            if (!id.valid())
                return;

            if (!mesh->isRenderable())
                return;

            const VkBuffer meshBuffer = resources.bufferFor(mesh->buffer_);
            const std::array<VkBuffer, Mesh::VertexStreams> vertexBuffers{meshBuffer, meshBuffer,
                                                                          meshBuffer, meshBuffer};

            vkCmdBindVertexBuffers(draw.cmd_, 0, streams, vertexBuffers.data(),
                                   mesh->streamOffsets_.data());
            vkCmdBindIndexBuffer(draw.cmd_, meshBuffer, mesh->streamOffsets_[Mesh::Index],
                                 toVkIndexType(mesh->indexFormat_));

            push.instanceIndex_ = id;
            if (mesh->subMeshCount == 0)
            {
                push.submeshIndex_ = 0;
                pushDraw(draw, push);
                vkCmdDrawIndexed(draw.cmd_, mesh->indexCount_, 1, 0, 0, 0);
                return;
            }
            for (uint8_t sub = 0; sub < mesh->subMeshCount; ++sub)
            {
                push.submeshIndex_ = sub;
                pushDraw(draw, push);
                const auto& subMesh = mesh->subMeshes[sub];
                vkCmdDrawIndexed(draw.cmd_, subMesh.indexCount, 1, subMesh.indexOffset, 0, 0);
            }
        });
}
}  // namespace batap
