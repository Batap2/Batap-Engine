#include "Renderer/Vulkan/Passes/GiUpdatePass.h"

#include "Renderer/ResourceFormat.h"
#include "Renderer/Vulkan/VulkanBarrier.h"
#include "Renderer/Vulkan/VulkanContext.h"
#include "Renderer/Vulkan/VulkanPipelines.h"
#include "Renderer/Vulkan/VulkanResources.h"
#include "Shaders/ShaderInterop.h"

#include <algorithm>
#include <optional>

namespace batap
{
namespace
{
constexpr int kResolveWidth = static_cast<int>(RtCacheResolveWidth);
constexpr int kResolveHeight = static_cast<int>(RtCacheCapacity / RtCacheResolveWidth);
}

GiUpdatePass::~GiUpdatePass()
{
    vkDestroyPipeline(setup_.ctx_.device_, updatePipeline_, nullptr);
    vkDestroyPipeline(setup_.ctx_.device_, resolvePipeline_, nullptr);
    vkDestroyQueryPool(setup_.ctx_.device_, queries_, nullptr);
}

void GiUpdatePass::buildPipelines(const ShaderModules& modules)
{
    vkDestroyPipeline(setup_.ctx_.device_, updatePipeline_, nullptr);
    vkDestroyPipeline(setup_.ctx_.device_, resolvePipeline_, nullptr);
    const auto build = [&](VkShaderModule pixelShader, VkFormat format)
    {
        return pixelShader ? GraphicsPipelineBuilder()
                                 .shaders(modules[SkyVS], pixelShader)
                                 .colorFormat(format)
                                 .build(setup_.ctx_.device_, setup_.rtLayout_)
                           : VK_NULL_HANDLE;
    };
    updatePipeline_ = build(modules[GiUpdatePS], VK_FORMAT_R8G8B8A8_UNORM);
    resolvePipeline_ = build(modules[GiResolvePS], VK_FORMAT_R8_UNORM);
}

void GiUpdatePass::resize(v2i frameSize)
{
    if (!resolveTarget_.valid())
        resolveTarget_ = setup_.resources_.createTarget(
            static_cast<uint32_t>(kResolveWidth), static_cast<uint32_t>(kResolveHeight),
            ResourceFormat::R8_UNORM, std::nullopt);

    const v2i size{std::max((frameSize.x() + 3) / 4, 1), std::max((frameSize.y() + 3) / 4, 1)};
    if (updateTarget_.valid() && size == size_)
        return;
    if (updateTarget_.valid())
        setup_.resources_.requestDestroy(updateTarget_);
    size_ = size;
    updateTarget_ = setup_.resources_.createTarget(static_cast<uint32_t>(size.x()),
                                                   static_cast<uint32_t>(size.y()),
                                                   ResourceFormat::R8G8B8A8_UNORM, std::nullopt);
}

void GiUpdatePass::record(const PassContext& pass, uint32_t frame, uint32_t passes)
{
    const VkDevice device = setup_.ctx_.device_;
    if (queries_ == VK_NULL_HANDLE)
    {
        VkQueryPoolCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        info.queryType = VK_QUERY_TYPE_TIMESTAMP;
        info.queryCount = 2 * FramesInFlight;
        if (vkCreateQueryPool(device, &info, nullptr, &queries_) != VK_SUCCESS)
            queries_ = VK_NULL_HANDLE;
        // A query is undefined until its first reset, which must be recorded.
        if (queries_ != VK_NULL_HANDLE)
            vkCmdResetQueryPool(pass.cmd_, queries_, 0, info.queryCount);
        written_.fill(false);
    }
    const uint32_t first = frame * 2;
    if (queries_ != VK_NULL_HANDLE && written_[frame])
    {
        uint64_t ticks[2]{};
        if (vkGetQueryPoolResults(device, queries_, first, 2, sizeof ticks, ticks,
                                  sizeof(uint64_t), VK_QUERY_RESULT_64_BIT) == VK_SUCCESS)
            gpuMs_ = static_cast<float>(ticks[1] - ticks[0]) * setup_.ctx_.timestampPeriodNs_ *
                     1e-6f;
        vkCmdResetQueryPool(pass.cmd_, queries_, first, 2);
    }
    if (queries_ != VK_NULL_HANDLE)
    {
        vkCmdWriteTimestamp(pass.cmd_, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, queries_, first);
        written_[frame] = true;
    }

    PassContext update = pass;
    for (uint32_t i = 0; i < std::max(passes, 1u); ++i)
    {
        update.push_.instanceIndex_ = i;
        draw(update, updateTarget_, size_, updatePipeline_);
        BarrierBatch{}.memory(Usage::ShaderWrite, Usage::ShaderWrite).flush(pass.cmd_);
    }
    draw(pass, resolveTarget_, v2i{kResolveWidth, kResolveHeight}, resolvePipeline_);

    if (queries_ != VK_NULL_HANDLE)
        vkCmdWriteTimestamp(pass.cmd_, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queries_, first + 1);
}

void GiUpdatePass::draw(const PassContext& pass, GPUResourceHandle target, v2i size,
                        VkPipeline pipeline)
{
    const VkImage image = setup_.resources_.imageFor(target);
    BarrierBatch{}
        .image(image, Usage::ShaderRead, Usage::ColorAttachment, colorRange(), Discard::Yes)
        .flush(pass.cmd_);

    VkRenderingAttachmentInfo color{};
    color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    color.imageView = setup_.resources_.viewFor(target);
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    const auto width = static_cast<uint32_t>(size.x());
    const auto height = static_cast<uint32_t>(size.y());
    VkRenderingInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    info.renderArea = {{0, 0}, {width, height}};
    info.layerCount = 1;
    info.colorAttachmentCount = 1;
    info.pColorAttachments = &color;

    vkCmdBeginRendering(pass.cmd_, &info);
    setViewportYUp(pass.cmd_, width, height);
    vkCmdBindPipeline(pass.cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    pushDraw(pass, pass.push_);
    vkCmdDraw(pass.cmd_, 3, 1, 0, 0);
    vkCmdEndRendering(pass.cmd_);

    BarrierBatch{}
        .image(image, Usage::ColorAttachment, Usage::ShaderRead, colorRange())
        .flush(pass.cmd_);
}
}  // namespace batap
