#include "Renderer/Vulkan/Passes/SkyPass.h"

#include "Renderer/Vulkan/VulkanContext.h"
#include "Renderer/Vulkan/VulkanPipelines.h"

namespace batap
{
SkyPass::~SkyPass()
{
    vkDestroyPipeline(setup_.ctx_.device_, pipeline_, nullptr);
}

void SkyPass::buildPipelines(const ShaderModules& modules)
{
    if (pipeline_)
        vkDestroyPipeline(setup_.ctx_.device_, pipeline_, nullptr);

    pipeline_ = GraphicsPipelineBuilder()
                    .shaders(modules[SkyVS], modules[SkyPS])
                    .colorFormat(setup_.colorFormat_)
                    .depth(setup_.depthFormat_, false, VK_COMPARE_OP_LESS_OR_EQUAL)
                    .build(setup_.ctx_.device_, setup_.layout_);
}

void SkyPass::record(const PassContext& pass)
{
    if (pass.push_.skyboxCount_ == 0)
        return;

    vkCmdBindPipeline(pass.cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    DrawPush push = pass.push_;
    push.instanceIndex_ = 0;
    pushDraw(pass, push);
    vkCmdDraw(pass.cmd_, 3, 1, 0, 0);
}
}  // namespace batap
