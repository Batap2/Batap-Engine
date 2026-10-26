#include "Renderer/Vulkan/Passes/ExposurePass.h"

#include "Components/Camera_C.h"
#include "Renderer/ResourceFormat.h"
#include "Renderer/Vulkan/RenderGraph.h"
#include "Renderer/Vulkan/VulkanContext.h"
#include "Renderer/Vulkan/VulkanPipelines.h"
#include "Renderer/Vulkan/VulkanResources.h"
#include "Shaders/ShaderInterop.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <span>

namespace batap
{
namespace
{
constexpr float kBrightenTau = 0.4f;
constexpr float kDarkenTau = 1.5f;
constexpr float kMiddleGrey = 0.18f;
}

ExposurePass::ExposurePass(const PassSetup& setup) : setup_(setup)
{
    ResourceManager& resources = setup_.resources_;
    grid_ = resources.createTarget(ExposureGridSize, ExposureGridSize,
                                   ResourceFormat::R32G32_FLOAT, "exposure grid");
    block_ = resources.createTarget(ExposureGridSize / ExposureReduceBlock,
                                    ExposureGridSize / ExposureReduceBlock,
                                    ResourceFormat::R32G32_FLOAT, "exposure block");
    total_ = resources.createTarget(1, 1, ResourceFormat::R32G32_FLOAT, "exposure total");
    for (GPUResourceHandle& readback : readback_)
        readback = resources.createMappedBuffer(2 * sizeof(float), "exposure readback");
}

ExposurePass::~ExposurePass()
{
    vkDestroyPipeline(setup_.ctx_.device_, measurePipeline_, nullptr);
    vkDestroyPipeline(setup_.ctx_.device_, reducePipeline_, nullptr);
}

void ExposurePass::buildPipelines(const ShaderModules& modules)
{
    vkDestroyPipeline(setup_.ctx_.device_, measurePipeline_, nullptr);
    vkDestroyPipeline(setup_.ctx_.device_, reducePipeline_, nullptr);
    const auto build = [&](VkShaderModule pixelShader)
    {
        return GraphicsPipelineBuilder()
            .shaders(modules[SkyVS], pixelShader)
            .colorFormat(VK_FORMAT_R32G32_SFLOAT)
            .build(setup_.ctx_.device_, setup_.layout_);
    };
    measurePipeline_ = build(modules[ExposureMeasurePS]);
    reducePipeline_ = build(modules[ExposureReducePS]);
}

float ExposurePass::update(uint32_t frame, entt::entity camera, const Camera_C& settings,
                           float dt)
{
    if (camera != camera_)
    {
        camera_ = camera;
        ev100_ = settings.ev100_;
        measured_.fill(false);
    }
    auto_ = settings.autoExposure_;
    if (!auto_)
    {
        ev100_ = settings.ev100_;
        measured_.fill(false);
        return exposureForEv100(ev100_);
    }

    if (measured_[frame])
    {
        float sums[2]{};
        std::memcpy(sums, setup_.resources_.mappedFor(readback_[frame]).data(), sizeof sums);
        if (sums[1] > 0.f)
        {
            const float averageLuminance = std::exp2(sums[0] / sums[1]);
            const float target =
                std::clamp(ev100ForExposure(kMiddleGrey / averageLuminance) -
                               settings.exposureCompensation_,
                           settings.minEv100_, std::max(settings.minEv100_, settings.maxEv100_));
            const float tau = target > ev100_ ? kBrightenTau : kDarkenTau;
            ev100_ += (target - ev100_) * (1.f - std::exp(-dt / tau));
        }
    }
    return exposureForEv100(ev100_);
}

void ExposurePass::addPasses(RenderGraph& graph, GraphImage hdr, const PassContext& pass,
                             uint32_t frame)
{
    if (!auto_)
        return;

    ResourceManager& resources = setup_.resources_;
    const auto importTarget = [&](const char* name, GPUResourceHandle target)
    { return graph.importImage(name, resources, target, Usage::ShaderRead, Usage::ShaderRead); };
    const GraphImage grid = importTarget("exposure grid", grid_);
    const GraphImage block = importTarget("exposure block", block_);
    const GraphImage total = importTarget("exposure total", total_);
    const GraphBuffer readback = graph.importBuffer("exposure readback", resources,
                                                    readback_[frame], Usage::None, Usage::None);

    const auto draw = [&](const char* name, GraphImage source, GraphImage target,
                          VkPipeline pipeline)
    {
        graph.addPass(name)
            .read(source, Usage::ShaderRead)
            .color(target, Load::DontCare)
            .execute(
                [pass, source, pipeline](GraphContext& c)
                {
                    PassContext context = pass;
                    context.cmd_ = c.cmd();
                    context.push_.instanceIndex_ = c.textureIndex(source);
                    vkCmdBindPipeline(context.cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                    pushDraw(context, context.push_);
                    vkCmdDraw(context.cmd_, 3, 1, 0, 0);
                });
    };
    draw("exposure measure", hdr, grid, measurePipeline_);
    draw("exposure reduce", grid, block, reducePipeline_);
    draw("exposure total", block, total, reducePipeline_);

    graph.addPass("exposure readback")
        .read(total, Usage::TransferSrc)
        .write(readback, Usage::TransferDst)
        .execute(
            [total, readback](GraphContext& c)
            {
                VkBufferImageCopy region{};
                region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                region.imageExtent = {1, 1, 1};
                vkCmdCopyImageToBuffer(c.cmd(), c.image(total),
                                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, c.buffer(readback), 1,
                                       &region);
            });
    measured_[frame] = true;
}
}  // namespace batap
