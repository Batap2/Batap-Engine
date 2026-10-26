#include "Renderer/Features/Exposure/Exposure.h"

#include "Components/Camera_C.h"
#include "Renderer/Device/Formats.h"
#include "Renderer/Device/Pipelines.h"
#include "Renderer/Device/ResourceManager.h"
#include "Renderer/Device/VulkanContext.h"
#include "Renderer/Graph/RenderGraph.h"
#include "Renderer/ResourceFormat.h"
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
}  // namespace

Exposure::Exposure(const DrawSetup& setup) : setup_(setup)
{
    for (GPUResourceHandle& readback : readback_)
        readback = setup_.resources_.createMappedBuffer(2 * sizeof(float), "exposure readback");
}

Exposure::~Exposure()
{
    vkDestroyPipeline(setup_.ctx_.device_, measurePipeline_, nullptr);
    vkDestroyPipeline(setup_.ctx_.device_, reducePipeline_, nullptr);
}

void Exposure::buildPipelines(const ShaderModules& modules)
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

float Exposure::update(uint32_t frame, entt::entity camera, const Camera_C& settings, float dt)
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
            const float target = std::clamp(
                ev100ForExposure(kMiddleGrey / averageLuminance) - settings.exposureCompensation_,
                settings.minEv100_, std::max(settings.minEv100_, settings.maxEv100_));
            const float tau = target > ev100_ ? kBrightenTau : kDarkenTau;
            ev100_ += (target - ev100_) * (1.f - std::exp(-dt / tau));
        }
    }
    return exposureForEv100(ev100_);
}

void Exposure::addPasses(RenderGraph& graph, GraphImage hdr, GraphBuffer frameConstants,
                         const DrawContext& draw, uint32_t frame)
{
    if (!auto_)
        return;

    const auto createTarget = [&](const char* name, uint32_t size)
    { return graph.createImage(name, {{size, size}, toVkFormat(ResourceFormat::R32G32_FLOAT)}); };
    const GraphImage grid = createTarget("exposure grid", ExposureGridSize);
    const GraphImage block = createTarget("exposure block", ExposureGridSize / ExposureReduceBlock);
    const GraphImage total = createTarget("exposure total", 1);
    const GraphBuffer readback = graph.importBuffer("exposure readback", setup_.resources_,
                                                    readback_[frame], Usage::None, Usage::None);

    const auto fullscreenPass = [&](const char* name, GraphImage source, GraphImage target,
                                    VkPipeline pipeline) -> GraphPass&
    {
        return graph.addPass(name)
            .read(source, Usage::ShaderRead)
            .color(target, Load::DontCare)
            .execute(
                [draw, source, pipeline](GraphContext& c)
                {
                    DrawContext context = bindDraw(draw, c.cmd());
                    context.push_.instanceIndex_ = c.textureIndex(source);
                    vkCmdBindPipeline(context.cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                    pushDraw(context, context.push_);
                    vkCmdDraw(context.cmd_, 3, 1, 0, 0);
                });
    };
    fullscreenPass("exposure measure", hdr, grid, measurePipeline_)
        .read(frameConstants, Usage::ShaderRead);
    fullscreenPass("exposure reduce", grid, block, reducePipeline_);
    fullscreenPass("exposure total", block, total, reducePipeline_);

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
