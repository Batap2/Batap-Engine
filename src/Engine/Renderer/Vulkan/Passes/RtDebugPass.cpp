#include "Renderer/Vulkan/Passes/RtDebugPass.h"

#include "Renderer/Vulkan/VulkanContext.h"
#include "Renderer/Vulkan/VulkanPipelines.h"

namespace batap
{
RtDebugPass::~RtDebugPass()
{
    vkDestroyPipeline(setup_.ctx_.device_, pipeline_, nullptr);
}

void RtDebugPass::buildPipelines(const ShaderModules& modules)
{
    vkDestroyPipeline(setup_.ctx_.device_, pipeline_, nullptr);
    pipeline_ = modules[RtDebugPS] ? GraphicsPipelineBuilder()
                                         .shaders(modules[SkyVS], modules[RtDebugPS])
                                         .colorFormat(setup_.colorFormat_)
                                         .depth(setup_.depthFormat_, false, VK_COMPARE_OP_ALWAYS)
                                         .build(setup_.ctx_.device_, setup_.rtLayout_)
                                   : VK_NULL_HANDLE;
}

void RtDebugPass::record(const PassContext& pass)
{
    vkCmdBindPipeline(pass.cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    pushDraw(pass, pass.push_);
    vkCmdDraw(pass.cmd_, 3, 1, 0, 0);
}
}  // namespace batap
