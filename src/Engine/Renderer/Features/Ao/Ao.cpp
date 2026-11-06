#include "Renderer/Features/Ao/Ao.h"

#include "Components/Camera_C.h"
#include "Renderer/Device/VulkanContext.h"

namespace batap
{
namespace
{
constexpr VkFormat AoRawFormat = VK_FORMAT_R16_SFLOAT;
constexpr VkFormat AoFormat = VK_FORMAT_R8_UNORM;

void drawFullscreen(const DrawContext& draw, GraphContext& c, VkPipeline pipeline)
{
    const DrawContext bound = bindDraw(draw, c.cmd());
    vkCmdBindPipeline(bound.cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    pushDraw(bound, bound.push_);
    vkCmdDraw(bound.cmd_, 3, 1, 0, 0);
}
}  // namespace

Ao::Ao(const DrawSetup& setup) : setup_(setup) {}

Ao::~Ao()
{
    vkDestroyPipeline(setup_.ctx_.device_, trace_, nullptr);
    vkDestroyPipeline(setup_.ctx_.device_, filter_, nullptr);
    vkDestroyPipeline(setup_.ctx_.device_, upsample_, nullptr);
}

void Ao::buildPipelines(const ShaderModules& modules)
{
    vkDestroyPipeline(setup_.ctx_.device_, trace_, nullptr);
    vkDestroyPipeline(setup_.ctx_.device_, filter_, nullptr);
    vkDestroyPipeline(setup_.ctx_.device_, upsample_, nullptr);
    trace_ = GraphicsPipelineBuilder()
                 .shaders(modules[SkyVS], modules[AoPS])
                 .colorFormat(AoRawFormat)
                 .build(setup_.ctx_.device_, setup_.layout_);
    filter_ = GraphicsPipelineBuilder()
                  .shaders(modules[SkyVS], modules[AoFilterPS])
                  .colorFormat(AoRawFormat)
                  .build(setup_.ctx_.device_, setup_.layout_);
    upsample_ = GraphicsPipelineBuilder()
                    .shaders(modules[SkyVS], modules[AoUpsamplePS])
                    .colorFormat(AoFormat)
                    .build(setup_.ctx_.device_, setup_.layout_);
}

std::optional<Ao::Images> Ao::createImages(RenderGraph& graph, VkExtent2D extent,
                                           const Camera_C& settings, bool enabled)
{
    images_.reset();
    if (!enabled || settings.aoStrength_ <= 0.f || settings.ortho())
        return images_;
    const VkExtent2D half{(extent.width + 1) / 2, (extent.height + 1) / 2};
    images_ = Images{graph.createImage("ao raw", {half, AoRawFormat}),
                     graph.createImage("ao filtered", {half, AoRawFormat}),
                     graph.createImage("ao", {extent, AoFormat})};
    return images_;
}

void Ao::addPasses(RenderGraph& graph, GraphImage depth, GraphImage normals,
                   GraphBuffer frameConstants, GraphBuffer uploads, const DrawContext& draw)
{
    if (!images_)
        return;
    graph.addPass("ao")
        .read(depth, Usage::ShaderRead)
        .read(normals, Usage::ShaderRead)
        .read(frameConstants, Usage::ShaderRead)
        .read(uploads, Usage::AnyRead)
        .color(images_->raw_, Load::DontCare)
        .execute([draw, pipeline = trace_](GraphContext& c) { drawFullscreen(draw, c, pipeline); });
    graph.addPass("ao filter")
        .read(images_->raw_, Usage::ShaderRead)
        .read(depth, Usage::ShaderRead)
        .read(normals, Usage::ShaderRead)
        .read(frameConstants, Usage::ShaderRead)
        .read(uploads, Usage::AnyRead)
        .color(images_->filtered_, Load::DontCare)
        .execute([draw, pipeline = filter_](GraphContext& c)
                 { drawFullscreen(draw, c, pipeline); });
    graph.addPass("ao upsample")
        .read(images_->filtered_, Usage::ShaderRead)
        .read(depth, Usage::ShaderRead)
        .read(normals, Usage::ShaderRead)
        .read(frameConstants, Usage::ShaderRead)
        .read(uploads, Usage::AnyRead)
        .color(images_->full_, Load::DontCare)
        .execute([draw, pipeline = upsample_](GraphContext& c)
                 { drawFullscreen(draw, c, pipeline); });
}
}  // namespace batap
