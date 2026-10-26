#include "Renderer/Vulkan/Passes/GiUpdatePass.h"

#include "Renderer/ResourceFormat.h"
#include "Renderer/Vulkan/VulkanContext.h"
#include "Renderer/Vulkan/VulkanPipelines.h"
#include "Renderer/Vulkan/VulkanResources.h"
#include "Shaders/ShaderInterop.h"

#include <algorithm>
#include <optional>
#include <string>

namespace batap
{
namespace
{
constexpr int kResolveWidth = static_cast<int>(RtCacheResolveWidth);
constexpr int kResolveHeight = static_cast<int>(RtCacheCapacity / RtCacheResolveWidth);
}  // namespace

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
        resolveTarget_ = setup_.resources_.createTarget(static_cast<uint32_t>(kResolveWidth),
                                                        static_cast<uint32_t>(kResolveHeight),
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

void GiUpdatePass::addPasses(RenderGraph& graph, const GiCacheBuffers& cache,
                             GraphBuffer frameConstants, const PassContext& pass, uint32_t frame,
                             uint32_t passes)
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
        // A query is undefined until its first reset, which must be recorded.
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

    ResourceManager& resources = setup_.resources_;
    const auto importTarget = [&](const char* name, GPUResourceHandle target)
    { return graph.importImage(name, resources, target, Usage::ShaderRead, Usage::ShaderRead); };
    const GraphImage updateTarget = importTarget("gi update target", updateTarget_);
    const GraphImage resolveTarget = importTarget("gi resolve target", resolveTarget_);

    const auto draw = [&](std::string name, GraphImage target, VkPipeline pipeline, uint32_t index)
    {
        graph.addPass(std::move(name))
            .write(cache.keys_, Usage::ShaderWrite)
            .write(cache.values_, Usage::ShaderWrite)
            .write(cache.stats_, Usage::ShaderWrite)
            .read(frameConstants, Usage::ShaderRead)
            .color(target, Load::DontCare)
            .execute(
                [pass, pipeline, index](GraphContext& c)
                {
                    PassContext context = pass;
                    context.cmd_ = c.cmd();
                    context.push_.instanceIndex_ = index;
                    vkCmdBindPipeline(context.cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                    pushDraw(context, context.push_);
                    vkCmdDraw(context.cmd_, 3, 1, 0, 0);
                });
    };
    for (uint32_t i = 0; i < std::max(passes, 1u); ++i)
        draw("gi update " + std::to_string(i), updateTarget, updatePipeline_, i);
    draw("gi resolve", resolveTarget, resolvePipeline_, pass.push_.instanceIndex_);

    if (queries != VK_NULL_HANDLE)
        graph.addPass("gi timer end")
            .execute(
                [queries, first](GraphContext& c)
                {
                    vkCmdWriteTimestamp(c.cmd(), VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queries,
                                        first + 1);
                });
}
}  // namespace batap
