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

namespace batap
{
namespace
{
constexpr float kBrightenTau = 0.4f;
constexpr float kDarkenTau = 1.5f;
constexpr float kMiddleGrey = 0.18f;
constexpr size_t kCells = ExposureGridSize * ExposureGridSize;
}  // namespace

Exposure::Exposure(const DrawSetup& setup) : setup_(setup), sums_(2 * kCells)
{
    cells_.reserve(kCells);
    for (GPUResourceHandle& readback : readback_)
        readback =
            setup_.resources_.createMappedBuffer(sums_.size() * sizeof(float), "exposure readback");
}

Exposure::~Exposure()
{
    vkDestroyPipeline(setup_.ctx_.device_, measurePipeline_, nullptr);
}

void Exposure::buildPipelines(const ShaderModules& modules)
{
    vkDestroyPipeline(setup_.ctx_.device_, measurePipeline_, nullptr);
    measurePipeline_ = GraphicsPipelineBuilder()
                           .shaders(modules[SkyVS], modules[ExposureMeasurePS])
                           .colorFormat(VK_FORMAT_R32G32_SFLOAT)
                           .build(setup_.ctx_.device_, setup_.layout_);
}

// Averaging the whole frame over-exposes as soon as a dark majority (black
// sky) pulls the mean down. Like the eye and matrix meters, anchor on the
// bright part: the luminance at `percentile`, with the cells more than `range`
// EV below it (space, dusk glow) and the few above it (sun, speculars) left out.
std::optional<float> Exposure::meteredLuminance(float percentile, float range)
{
    if (cells_.empty())
        return std::nullopt;
    std::ranges::sort(cells_, {}, &Cell::logLuminance_);

    float total = 0.f;
    for (const Cell& cell : cells_)
        total += cell.weight_;
    float top = cells_.back().logLuminance_;
    float cumulative = 0.f;
    for (const Cell& cell : cells_)
    {
        cumulative += cell.weight_;
        if (cumulative >= percentile * total)
        {
            top = cell.logLuminance_;
            break;
        }
    }

    float sum = 0.f;
    float weight = 0.f;
    for (const Cell& cell : cells_)
    {
        if (cell.logLuminance_ > top)
            break;
        if (cell.logLuminance_ >= top - range)
        {
            sum += cell.logLuminance_ * cell.weight_;
            weight += cell.weight_;
        }
    }
    return std::exp2(sum / weight);
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
        std::memcpy(sums_.data(), setup_.resources_.mappedFor(readback_[frame]).data(),
                    sums_.size() * sizeof(float));
        cells_.clear();
        for (size_t i = 0; i < kCells; ++i)
            if (const float count = sums_[2 * i + 1]; count > 0.f)
                cells_.push_back({sums_[2 * i] / count, count});
        if (const std::optional<float> luminance =
                meteredLuminance(settings.meteringPercentile_, settings.meteringRange_))
        {
            const float target = std::clamp(
                ev100ForExposure(kMiddleGrey / *luminance) - settings.exposureCompensation_,
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

    const GraphImage grid =
        graph.createImage("exposure grid", {{ExposureGridSize, ExposureGridSize},
                                            toVkFormat(ResourceFormat::R32G32_FLOAT)});
    const GraphBuffer readback = graph.importBuffer("exposure readback", setup_.resources_,
                                                    readback_[frame], Usage::None, Usage::None);

    graph.addPass("exposure measure")
        .read(hdr, Usage::ShaderRead)
        .read(frameConstants, Usage::ShaderRead)
        .color(grid, Load::DontCare)
        .execute(
            [draw, pipeline = measurePipeline_](GraphContext& c)
            {
                const DrawContext context = bindDraw(draw, c.cmd());
                vkCmdBindPipeline(context.cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                vkCmdDraw(context.cmd_, 3, 1, 0, 0);
            });

    graph.addPass("exposure readback")
        .read(grid, Usage::TransferSrc)
        .write(readback, Usage::TransferDst)
        .execute(
            [grid, readback](GraphContext& c)
            {
                VkBufferImageCopy region{};
                region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                region.imageExtent = {ExposureGridSize, ExposureGridSize, 1};
                vkCmdCopyImageToBuffer(c.cmd(), c.image(grid), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                       c.buffer(readback), 1, &region);
            });
    measured_[frame] = true;
}
}  // namespace batap
