#include "Renderer/Features/Ao/Ao.h"

#include "Components/Camera_C.h"
#include "Renderer/Device/VulkanContext.h"

namespace batap
{
namespace
{
constexpr VkFormat AoFormat = VK_FORMAT_R8_UNORM;
}

Ao::Ao(const DrawSetup& setup) : setup_(setup) {}

Ao::~Ao()
{
    vkDestroyPipeline(setup_.ctx_.device_, pipeline_, nullptr);
}

void Ao::buildPipelines(const ShaderModules& modules)
{
    vkDestroyPipeline(setup_.ctx_.device_, pipeline_, nullptr);
    pipeline_ = GraphicsPipelineBuilder()
                    .shaders(modules[SkyVS], modules[AoPS])
                    .colorFormat(AoFormat)
                    .build(setup_.ctx_.device_, setup_.layout_);
}

std::optional<GraphImage> Ao::createImage(RenderGraph& graph, VkExtent2D extent,
                                          const Camera_C& settings)
{
    image_.reset();
    if (settings.aoStrength_ > 0.f && !settings.ortho())
        image_ = graph.createImage("ao", {extent, AoFormat});
    return image_;
}

void Ao::addPass(RenderGraph& graph, GraphImage depth, GraphImage normals,
                 GraphBuffer frameConstants, GraphBuffer uploads, const DrawContext& draw)
{
    if (!image_)
        return;
    graph.addPass("ao")
        .read(depth, Usage::ShaderRead)
        .read(normals, Usage::ShaderRead)
        .read(frameConstants, Usage::ShaderRead)
        .read(uploads, Usage::AnyRead)
        .color(*image_, Load::DontCare)
        .execute(
            [draw, pipeline = pipeline_](GraphContext& c)
            {
                const DrawContext bound = bindDraw(draw, c.cmd());
                vkCmdBindPipeline(bound.cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                pushDraw(bound, bound.push_);
                vkCmdDraw(bound.cmd_, 3, 1, 0, 0);
            });
}
}  // namespace batap
