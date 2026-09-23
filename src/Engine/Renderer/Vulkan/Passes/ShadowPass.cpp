#include "Renderer/Vulkan/Passes/ShadowPass.h"

#include "Assets/Mesh.h"
#include "Flatten.h"
#include "Renderer/EngineConfig.h"
#include "Renderer/Vulkan/Passes/MeshDraws.h"
#include "Renderer/Vulkan/VulkanBarrier.h"
#include "Renderer/Vulkan/VulkanContext.h"
#include "Renderer/Vulkan/VulkanPipelines.h"
#include "Renderer/Vulkan/VulkanResources.h"

#include <cstring>

namespace batap
{
namespace
{
// Room for the views of step 8 (a 4096 atlas holds at most 1024 tiles of 128)
// without resizing on the way there.
constexpr uint32_t kMaxShadowViews = 256;
}  // namespace

ShadowPass::ShadowPass(const PassSetup& setup) : setup_(setup)
{
    buffer_ = setup_.resources_.createPerFrameBuffer(sizeof(ShadowGPUData) * kMaxShadowViews,
                                                     "shadows");
}

ShadowPass::~ShadowPass()
{
    vkDestroyPipeline(setup_.ctx_.device_, pipeline_, nullptr);
}

void ShadowPass::buildPipelines(const ShaderModules& modules)
{
    if (pipeline_)
        vkDestroyPipeline(setup_.ctx_.device_, pipeline_, nullptr);

    pipeline_ = GraphicsPipelineBuilder()
                    .shaders(modules[ShadowVS], VK_NULL_HANDLE)
                    .vertexAttribute(Mesh::Position, VK_FORMAT_R32G32B32_SFLOAT, 12)
                    .depth(VK_FORMAT_D32_SFLOAT, true, VK_COMPARE_OP_LESS)
                    .depthOnly()
                    .depthClamp()
                    .dynamicDepthBias()
                    .cullBack()
                    .build(setup_.ctx_.device_, setup_.layout_);
}

void ShadowPass::record(const PassContext& pass, VkImage atlas, VkImageView atlasView,
                        const m4f& viewProj)
{
    PassContext shadowPass = pass;
    flatten(shadowPass.push_.shadowViewProj_, viewProj);

    ShadowGPUData entry{};
    flatten(entry.viewProj_, viewProj);
    entry.atlasIndex_ = 0;
    entry.texelWorld_ = 2.f / static_cast<float>(LocalTileMax);
    std::memcpy(setup_.resources_.requestUpload(buffer_, sizeof(entry)).data(), &entry,
                sizeof(entry));

    // Discard: the scope clears the whole atlas, so only last frame's reads have
    // to finish — its contents do not have to survive. Also covers the first
    // frame, where the image has no layout yet.
    BarrierBatch{}
        .image(atlas, Usage::ShaderRead, Usage::DepthAttachment, depthRange(), Discard::Yes)
        .flush(pass.cmd_);

    VkRenderingAttachmentInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depth.imageView = atlasView;
    depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    // 1 = the light sees infinitely far, so an untouched texel lights whatever
    // samples it.
    depth.clearValue.depthStencil = {1.0f, 0};

    VkRenderingInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    info.renderArea = {{0, 0}, {LocalAtlasSize, LocalAtlasSize}};
    info.layerCount = 1;
    info.pDepthAttachment = &depth;

    vkCmdBeginRendering(pass.cmd_, &info);
    vkCmdBindPipeline(pass.cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vkCmdSetDepthBias(pass.cmd_, 0.0f, 0.0f, 0.0f);
    setViewportYUpRect(pass.cmd_, 0, 0, LocalTileMax, LocalTileMax);

    recordMeshDraws(shadowPass, setup_.resources_, 1);

    vkCmdEndRendering(pass.cmd_);

    BarrierBatch{}
        .image(atlas, Usage::DepthAttachment, Usage::ShaderRead, depthRange())
        .flush(pass.cmd_);
}
}  // namespace batap
