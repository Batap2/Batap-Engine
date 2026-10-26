#include "Renderer/Draws/FullscreenDrawers.h"

#include "Renderer/Device/Pipelines.h"
#include "Renderer/Device/VulkanContext.h"

namespace batap
{
FullscreenDrawers::~FullscreenDrawers()
{
    vkDestroyPipeline(setup_.ctx_.device_, sky_, nullptr);
    vkDestroyPipeline(setup_.ctx_.device_, tonemap_, nullptr);
    vkDestroyPipeline(setup_.ctx_.device_, rtFirstHit_, nullptr);
}

void FullscreenDrawers::buildPipelines(const ShaderModules& modules)
{
    const VkDevice device = setup_.ctx_.device_;
    vkDestroyPipeline(device, sky_, nullptr);
    vkDestroyPipeline(device, tonemap_, nullptr);
    vkDestroyPipeline(device, rtFirstHit_, nullptr);

    sky_ = GraphicsPipelineBuilder()
               .shaders(modules[SkyVS], modules[SkyPS])
               .colorFormat(setup_.colorFormat_)
               .depth(setup_.depthFormat_, false, VK_COMPARE_OP_GREATER_OR_EQUAL)
               .build(device, setup_.layout_);
    tonemap_ = GraphicsPipelineBuilder()
                   .shaders(modules[SkyVS], modules[TonemapPS])
                   .colorFormat(outputFormat_)
                   .depth(setup_.depthFormat_, false, VK_COMPARE_OP_ALWAYS)
                   .build(device, setup_.layout_);
    rtFirstHit_ = modules[RtDebugPS] ? GraphicsPipelineBuilder()
                                           .shaders(modules[SkyVS], modules[RtDebugPS])
                                           .colorFormat(setup_.colorFormat_)
                                           .depth(setup_.depthFormat_, false, VK_COMPARE_OP_ALWAYS)
                                           .build(device, setup_.rtLayout_)
                                     : VK_NULL_HANDLE;
}

void FullscreenDrawers::recordSky(const DrawContext& draw)
{
    if (draw.push_.skyboxCount_ == 0)
        return;

    vkCmdBindPipeline(draw.cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, sky_);
    DrawPush push = draw.push_;
    push.instanceIndex_ = 0;
    pushDraw(draw, push);
    vkCmdDraw(draw.cmd_, 3, 1, 0, 0);
}

void FullscreenDrawers::recordTonemap(const DrawContext& draw)
{
    vkCmdBindPipeline(draw.cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, tonemap_);
    pushDraw(draw, draw.push_);
    vkCmdDraw(draw.cmd_, 3, 1, 0, 0);
}

void FullscreenDrawers::recordRtFirstHit(const DrawContext& draw)
{
    vkCmdBindPipeline(draw.cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, rtFirstHit_);
    pushDraw(draw, draw.push_);
    vkCmdDraw(draw.cmd_, 3, 1, 0, 0);
}
}  // namespace batap
