#include "Renderer/Features/Gi/GiUpdate.h"

#include "Renderer/Device/Formats.h"
#include "Renderer/Device/Pipelines.h"
#include "Renderer/Device/ResourceManager.h"
#include "Renderer/Device/VulkanContext.h"
#include "Renderer/ResourceFormat.h"
#include "Shaders/ShaderInterop.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>

namespace batap
{
namespace
{
constexpr int kResolveWidth = static_cast<int>(RtCacheResolveWidth);
constexpr int kResolveHeight = static_cast<int>(RtCacheCapacity / RtCacheResolveWidth);
}  // namespace

GiUpdate::~GiUpdate()
{
    vkDestroyPipeline(setup_.ctx_.device_, updatePipeline_, nullptr);
    vkDestroyPipeline(setup_.ctx_.device_, resolvePipeline_, nullptr);
    vkDestroyQueryPool(setup_.ctx_.device_, queries_, nullptr);
}

void GiUpdate::buildPipelines(const ShaderModules& modules)
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

VkExtent2D GiUpdate::updateExtent(VkExtent2D output)
{
    return {std::max((output.width + 3) / 4, 1u), std::max((output.height + 3) / 4, 1u)};
}

void GiUpdate::writeFrameData(FrameGPUData& data, VkExtent2D output, float dt, uint32_t bounces,
                              uint32_t passes)
{
    passes_ = std::clamp(passes, 1u, 8u);
    const VkExtent2D extent = updateExtent(output);
    data.frameIndex_ = ++frame_;
    data.giWidth_ = extent.width;
    data.giHeight_ = extent.height;
    data.giMaxAge_ = static_cast<uint32_t>(std::lround(3.f / dt));
    data.giDt_ = dt;
    data.giBounces_ = std::clamp(bounces, 1u, 4u);
    data.giPasses_ = passes_;
}

void GiUpdate::addPasses(RenderGraph& graph, GraphBuffer tlas, GraphBuffer blases,
                         GraphBuffer traced, const GiCache::Buffers& cache, GraphBuffer uploads,
                         GraphBuffer frameConstants, const DrawContext& draw, uint32_t frame,
                         VkExtent2D output)
{
    const VkDevice device = setup_.ctx_.device_;
    const uint32_t first = frame * 2;
    uint32_t resetFirst = first;
    uint32_t resetCount = 0;
    if (queries_ == VK_NULL_HANDLE)
    {
        VkQueryPoolCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        info.queryType = VK_QUERY_TYPE_TIMESTAMP;
        info.queryCount = 2 * FramesInFlight;
        if (vkCreateQueryPool(device, &info, nullptr, &queries_) != VK_SUCCESS)
            queries_ = VK_NULL_HANDLE;
        resetFirst = 0;
        resetCount = info.queryCount;
        written_.fill(false);
    }
    else if (written_[frame])
    {
        uint64_t ticks[2]{};
        if (vkGetQueryPoolResults(device, queries_, first, 2, sizeof ticks, ticks, sizeof(uint64_t),
                                  VK_QUERY_RESULT_64_BIT) == VK_SUCCESS)
            gpuMs_ =
                static_cast<float>(ticks[1] - ticks[0]) * setup_.ctx_.timestampPeriodNs_ * 1e-6f;
        resetCount = 2;
    }
    const VkQueryPool queries = queries_;
    if (queries != VK_NULL_HANDLE)
    {
        graph.addPass("gi timer start")
            .execute(
                [queries, first, resetFirst, resetCount](GraphContext& c)
                {
                    if (resetCount != 0)
                        vkCmdResetQueryPool(c.cmd(), queries, resetFirst, resetCount);
                    vkCmdWriteTimestamp(c.cmd(), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, queries, first);
                });
        written_[frame] = true;
    }

    const GraphImage updateTarget = graph.createImage(
        "gi update target", {updateExtent(output), toVkFormat(ResourceFormat::R8G8B8A8_UNORM)});
    const GraphImage resolveTarget = graph.createImage(
        "gi resolve target",
        {{static_cast<uint32_t>(kResolveWidth), static_cast<uint32_t>(kResolveHeight)},
         toVkFormat(ResourceFormat::R8_UNORM)});

    const auto fullscreenPass = [&](std::string name, GraphImage target, VkPipeline pipeline,
                                    uint32_t index) -> GraphPass&
    {
        return graph.addPass(std::move(name))
            .write(cache.keys_, Usage::ShaderWrite)
            .write(cache.values_, Usage::ShaderWrite)
            .write(cache.stats_, Usage::ShaderWrite)
            .read(frameConstants, Usage::ShaderRead)
            .color(target, Load::DontCare)
            .execute(
                [draw, pipeline, index](GraphContext& c)
                {
                    DrawContext context = bindDraw(draw, c.cmd());
                    context.push_.instanceIndex_ = index;
                    vkCmdBindPipeline(context.cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                    pushDraw(context, context.push_);
                    vkCmdDraw(context.cmd_, 3, 1, 0, 0);
                });
    };
    for (uint32_t i = 0; i < passes_; ++i)
        fullscreenPass("gi update " + std::to_string(i), updateTarget, updatePipeline_, i)
            .read(uploads, Usage::AnyRead)
            .read(tlas, Usage::AccelerationStructureRead)
            .read(blases, Usage::AccelerationStructureRead)
            .read(traced, Usage::ShaderRead);
    fullscreenPass("gi resolve", resolveTarget, resolvePipeline_, draw.push_.instanceIndex_);

    if (queries != VK_NULL_HANDLE)
        graph.addPass("gi timer end")
            .execute(
                [queries, first](GraphContext& c)
                {
                    vkCmdWriteTimestamp(c.cmd(), VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queries,
                                        first + 1);
                });
}

void GiUpdate::report(const GiCache::Stats& stats, bool show) const
{
    if (!show || frame_ % 120 != 0)
        return;
    std::cout << "[GI] cache: " << stats.occupied_ << " / " << RtCacheCapacity << " cells, +"
              << stats.inserted_ << " inserted, " << stats.failed_ << " failed, " << stats.evicted_
              << " evicted, " << stats.tailMiss_ << " tail misses; " << passes_ << " passes, "
              << gpuMs_ << " ms\n";
}
}  // namespace batap
