#include "Renderer/Vulkan/Passes/GiUpdatePass.h"

#include "Renderer/ResourceFormat.h"
#include "Renderer/Vulkan/VulkanBarrier.h"
#include "Renderer/Vulkan/VulkanContext.h"
#include "Renderer/Vulkan/VulkanPipelines.h"
#include "Renderer/Vulkan/VulkanResources.h"

#include <algorithm>
#include <optional>

namespace batap
{
GiUpdatePass::~GiUpdatePass()
{
    vkDestroyPipeline(setup_.ctx_.device_, pipeline_, nullptr);
}

void GiUpdatePass::buildPipelines(const ShaderModules& modules)
{
    vkDestroyPipeline(setup_.ctx_.device_, pipeline_, nullptr);
    pipeline_ = modules[GiUpdatePS] ? GraphicsPipelineBuilder()
                                          .shaders(modules[SkyVS], modules[GiUpdatePS])
                                          .colorFormat(VK_FORMAT_R8G8B8A8_UNORM)
                                          .build(setup_.ctx_.device_, setup_.rtLayout_)
                                    : VK_NULL_HANDLE;
}

void GiUpdatePass::resize(v2i frameSize)
{
    const v2i size{std::max((frameSize.x() + 3) / 4, 1), std::max((frameSize.y() + 3) / 4, 1)};
    if (target_.valid() && size == size_)
        return;
    if (target_.valid())
        setup_.resources_.requestDestroy(target_);
    size_ = size;
    target_ = setup_.resources_.createTarget(static_cast<uint32_t>(size.x()),
                                             static_cast<uint32_t>(size.y()),
                                             ResourceFormat::R8G8B8A8_UNORM, std::nullopt);
}

void GiUpdatePass::record(const PassContext& pass)
{
    const VkImage image = setup_.resources_.imageFor(target_);
    BarrierBatch{}
        .image(image, Usage::ShaderRead, Usage::ColorAttachment, colorRange(), Discard::Yes)
        .flush(pass.cmd_);

    VkRenderingAttachmentInfo color{};
    color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    color.imageView = setup_.resources_.viewFor(target_);
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    const auto width = static_cast<uint32_t>(size_.x());
    const auto height = static_cast<uint32_t>(size_.y());
    VkRenderingInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    info.renderArea = {{0, 0}, {width, height}};
    info.layerCount = 1;
    info.colorAttachmentCount = 1;
    info.pColorAttachments = &color;

    vkCmdBeginRendering(pass.cmd_, &info);
    setViewportYUp(pass.cmd_, width, height);
    vkCmdBindPipeline(pass.cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    pushDraw(pass, pass.push_);
    vkCmdDraw(pass.cmd_, 3, 1, 0, 0);
    vkCmdEndRendering(pass.cmd_);

    BarrierBatch{}
        .image(image, Usage::ColorAttachment, Usage::ShaderRead, colorRange())
        .flush(pass.cmd_);
}
}  // namespace batap
