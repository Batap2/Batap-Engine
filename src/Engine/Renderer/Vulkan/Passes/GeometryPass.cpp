#include "Renderer/Vulkan/Passes/GeometryPass.h"

#include "Assets/AssetManager.h"
#include "Assets/Mesh.h"
#include "Components/Mesh_C.h"
#include "Instance/InstanceManager.h"
#include "Renderer/Vulkan/VulkanContext.h"
#include "Renderer/Vulkan/VulkanFormats.h"
#include "Renderer/Vulkan/VulkanPipelines.h"
#include "Renderer/Vulkan/VulkanResources.h"

#include <array>

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

    DrawPush push = pass.push_;
    pass.reg_->view<Mesh_C>().each(
        [&](entt::entity e, Mesh_C& meshC)
        {
            if (!meshC.mesh_)
                return;
            auto* mesh = pass.assetManager_->get(meshC.mesh_);

            const auto id =
                pass.instanceManager_->pool<StaticMeshInstance>().getGPUIndex({pass.reg_, e});
            if (!id.valid())
                return;

            if (!mesh->isRenderable())
                return;  // mesh incomplet (pas de normales/uv/tangentes)

            // One resolution for the whole mesh: every stream is in the same
            // buffer, only the offsets differ — and they are already stored in
            // binding order.
            const VkBuffer meshBuffer = setup_.resources_.bufferFor(mesh->buffer_);
            const std::array<VkBuffer, Mesh::VertexStreams> vertexBuffers{meshBuffer, meshBuffer,
                                                                          meshBuffer, meshBuffer};

            vkCmdBindVertexBuffers(pass.cmd_, 0, Mesh::VertexStreams, vertexBuffers.data(),
                                   mesh->streamOffsets_.data());
            vkCmdBindIndexBuffer(pass.cmd_, meshBuffer, mesh->streamOffsets_[Mesh::Index],
                                 toVkIndexType(mesh->indexFormat_));

            // Un draw par submesh, l'index de submesh en push constant (le PS
            // y lit le matériau — pas de SV_PrimitiveID sur Metal)
            push.instanceIndex_ = id;
            if (mesh->subMeshCount == 0)
            {
                push.submeshIndex_ = 0;
                pushDraw(pass, push);
                vkCmdDrawIndexed(pass.cmd_, mesh->indexCount_, 1, 0, 0, 0);
                return;
            }
            for (uint8_t sub = 0; sub < mesh->subMeshCount; ++sub)
            {
                push.submeshIndex_ = sub;
                pushDraw(pass, push);
                const auto& subMesh = mesh->subMeshes[sub];
                vkCmdDrawIndexed(pass.cmd_, subMesh.indexCount, 1, subMesh.indexOffset, 0, 0);
            }
        });
}
}  // namespace batap
