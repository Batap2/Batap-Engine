#include "Renderer/Vulkan/Passes/ShadowPass.h"

#include "Assets/Mesh.h"
#include "Flatten.h"
#include "Renderer/EngineConfig.h"
#include "Renderer/Vulkan/Passes/MeshDraws.h"
#include "Renderer/Vulkan/VulkanBarrier.h"
#include "Renderer/Vulkan/VulkanContext.h"
#include "Renderer/Vulkan/VulkanPipelines.h"
#include "Renderer/Vulkan/VulkanResources.h"

#include <algorithm>

namespace batap
{
namespace
{
constexpr size_t kInitialEntries = 256;

// vkCmdUpdateBuffer's cap.
constexpr size_t kEntriesPerWrite = 65536 / sizeof(ShadowGPUData);

constexpr float kDepthBiasSlope = 2.f;
}  // namespace

ShadowPass::ShadowPass(const PassSetup& setup) : setup_(setup)
{
    reserve(kInitialEntries);
}

void ShadowPass::reserve(size_t entryCount)
{
    if (entryCount <= capacity_)
        return;

    capacity_ = std::max(kInitialEntries, std::bit_ceil(entryCount));

    if (buffer_.valid())
        setup_.resources_.requestDestroy(buffer_);
    buffer_ = setup_.resources_.createPerFrameBuffer(sizeof(ShadowGPUData) * capacity_, "shadows");
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

void ShadowPass::record(const PassContext& pass, GPUResourceHandle atlas,
                        std::span<const ShadowView> views, size_t entryCount)
{
    if (entryCount == 0 || entryCount > capacity_)
        return;

    const VkImage atlasImage = setup_.resources_.imageFor(atlas);
    const uint32_t atlasTexture = setup_.resources_.textureIndex(atlas);
    const float atlasSize = static_cast<float>(LocalAtlasSize);

    entries_.assign(entryCount, ShadowGPUData{});
    for (const ShadowView& view : views)
    {
        if (view.entry_ >= entryCount)
            continue;
        ShadowGPUData& entry = entries_[view.entry_];
        flatten(entry.viewProj_, view.viewProj_);
        entry.strength_ = view.strength_;
        entry.texelWorld_ = view.texelWorld_;
        entry.atlasTexture_ = atlasTexture;
        entry.uvScale_ = static_cast<float>(view.size_) / atlasSize;
        entry.uvOffset_[0] = static_cast<float>(view.x_) / atlasSize;
        entry.uvOffset_[1] = static_cast<float>(view.y_) / atlasSize;
        entry.texelUV_ = 1.f / atlasSize;
    }
    const std::span<const ShadowGPUData> all{entries_};
    for (size_t first = 0; first < all.size(); first += kEntriesPerWrite)
    {
        const auto chunk = all.subspan(first, std::min(kEntriesPerWrite, all.size() - first));
        setup_.resources_.recordBufferWrite(pass.cmd_, buffer_, chunk.data(), chunk.size_bytes(),
                                            first * sizeof(ShadowGPUData));
    }

    if (views.empty())
    {
        BarrierBatch{}.memory(Usage::TransferDst, Usage::ShaderRead).flush(pass.cmd_);
        return;
    }

    // Discard: the scope clears the whole atlas, so only last frame's reads have
    // to finish — its contents do not have to survive. Also covers the first
    // frame, where the image has no layout yet.
    BarrierBatch{}
        .memory(Usage::TransferDst, Usage::ShaderRead)
        .image(atlasImage, Usage::ShaderRead, Usage::DepthAttachment, depthRange(), Discard::Yes)
        .flush(pass.cmd_);

    VkRenderingAttachmentInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depth.imageView = setup_.resources_.viewFor(atlas);
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
    vkCmdSetDepthBias(pass.cmd_, 0.f, 0.f, kDepthBiasSlope);

    PassContext shadowPass = pass;
    for (const ShadowView& view : views)
    {
        shadowPass.push_.shadowViewIndex_ = view.entry_;
        setViewportYUpRect(pass.cmd_, view.x_, view.y_, view.size_, view.size_);
        recordMeshDraws(shadowPass, setup_.resources_, 1);
    }

    vkCmdEndRendering(pass.cmd_);

    BarrierBatch{}
        .image(atlasImage, Usage::DepthAttachment, Usage::ShaderRead, depthRange())
        .flush(pass.cmd_);
}
}  // namespace batap
