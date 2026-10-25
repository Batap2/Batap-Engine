#include "Renderer/Vulkan/Passes/ExposurePass.h"

#include "Components/Camera_C.h"
#include "Renderer/ResourceFormat.h"
#include "Renderer/Vulkan/VulkanBarrier.h"
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

void ExposurePass::record(const PassContext& pass, uint32_t frame)
{
    if (!auto_)
        return;

    ResourceManager& resources = setup_.resources_;
    draw(pass, grid_, ExposureGridSize, measurePipeline_, 0);
    draw(pass, block_, ExposureGridSize / ExposureReduceBlock, reducePipeline_,
         resources.textureIndex(grid_));
    draw(pass, total_, 1, reducePipeline_, resources.textureIndex(block_));

    const VkImage total = resources.imageFor(total_);
    BarrierBatch{}.image(total, Usage::ShaderRead, Usage::TransferSrc, colorRange()).flush(pass.cmd_);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {1, 1, 1};
    vkCmdCopyImageToBuffer(pass.cmd_, total, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           resources.bufferFor(readback_[frame]), 1, &region);
    BarrierBatch{}.image(total, Usage::TransferSrc, Usage::ShaderRead, colorRange()).flush(pass.cmd_);
    measured_[frame] = true;
}

void ExposurePass::draw(const PassContext& pass, GPUResourceHandle target, uint32_t size,
                        VkPipeline pipeline, uint32_t source)
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

    VkRenderingInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    info.renderArea = {{0, 0}, {size, size}};
    info.layerCount = 1;
    info.colorAttachmentCount = 1;
    info.pColorAttachments = &color;

    vkCmdBeginRendering(pass.cmd_, &info);
    setViewportYUp(pass.cmd_, size, size);
    vkCmdBindPipeline(pass.cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    DrawPush push = pass.push_;
    push.instanceIndex_ = source;
    pushDraw(pass, push);
    vkCmdDraw(pass.cmd_, 3, 1, 0, 0);
    vkCmdEndRendering(pass.cmd_);

    BarrierBatch{}
        .image(image, Usage::ColorAttachment, Usage::ShaderRead, colorRange())
        .flush(pass.cmd_);
}
}  // namespace batap
