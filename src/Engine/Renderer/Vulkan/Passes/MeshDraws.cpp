#include "Renderer/Vulkan/Passes/MeshDraws.h"

#include "Assets/AssetManager.h"
#include "Assets/Mesh.h"
#include "Components/Mesh_C.h"
#include "Instance/InstanceManager.h"
#include "Renderer/Vulkan/Passes/Pass.h"
#include "Renderer/Vulkan/VulkanFormats.h"
#include "Renderer/Vulkan/VulkanResources.h"

#include <array>

namespace batap
{
void recordMeshDraws(const PassContext& pass, ResourceManager& resources, uint32_t streams)
{
    DrawPush push = pass.push_;
    pass.reg_->view<Mesh_C>().each(
        [&](entt::entity e, Mesh_C& meshC)
        {
            if (!meshC.mesh_)
                return;
            auto* mesh = pass.assetManager_->get(meshC.mesh_);
            if (!mesh)
                return;

            const auto id =
                pass.instanceManager_->pool<StaticMeshInstance>().getGPUIndex({pass.reg_, e});
            if (!id.valid())
                return;

            if (!mesh->isRenderable())
                return;

            // One resolution for the whole mesh: every stream is in the same
            // buffer, only the offsets differ — and they are already stored in
            // binding order.
            const VkBuffer meshBuffer = resources.bufferFor(mesh->buffer_);
            const std::array<VkBuffer, Mesh::VertexStreams> vertexBuffers{meshBuffer, meshBuffer,
                                                                          meshBuffer, meshBuffer};

            vkCmdBindVertexBuffers(pass.cmd_, 0, streams, vertexBuffers.data(),
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
