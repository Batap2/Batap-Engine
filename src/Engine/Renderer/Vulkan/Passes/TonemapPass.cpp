#include "Renderer/Vulkan/Passes/TonemapPass.h"

#include "Renderer/Vulkan/VulkanContext.h"
#include "Renderer/Vulkan/VulkanPipelines.h"

namespace batap
{
TonemapPass::~TonemapPass()
{
    vkDestroyPipeline(setup_.ctx_.device_, pipeline_, nullptr);
}

void TonemapPass::buildPipelines(const ShaderModules& modules)
{
    if (pipeline_)
        vkDestroyPipeline(setup_.ctx_.device_, pipeline_, nullptr);

    pipeline_ = GraphicsPipelineBuilder()
                    .shaders(modules[SkyVS], modules[TonemapPS])
                    .colorFormat(setup_.colorFormat_)
                    .depth(setup_.depthFormat_, false, VK_COMPARE_OP_ALWAYS)
                    .build(setup_.ctx_.device_, setup_.layout_);
}

void TonemapPass::record(const PassContext& pass)
{
    vkCmdBindPipeline(pass.cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    pushDraw(pass, pass.push_);
    vkCmdDraw(pass.cmd_, 3, 1, 0, 0);
}
}  // namespace batap
