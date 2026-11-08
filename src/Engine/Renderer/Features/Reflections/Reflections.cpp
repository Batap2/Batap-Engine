#include "Renderer/Features/Reflections/Reflections.h"

#include "Components/Camera_C.h"
#include "Renderer/Device/Pipelines.h"
#include "Renderer/Device/VulkanContext.h"

namespace batap
{
namespace
{
constexpr VkFormat ReflectRadianceFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr VkFormat ReflectRayFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

void drawFullscreen(const DrawContext& draw, GraphContext& c, VkPipeline pipeline)
{
    const DrawContext bound = bindDraw(draw, c.cmd());
    vkCmdBindPipeline(bound.cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    pushDraw(bound, bound.push_);
    vkCmdDraw(bound.cmd_, 3, 1, 0, 0);
}
}  // namespace

Reflections::Reflections(const DrawSetup& setup) : setup_(setup) {}

Reflections::~Reflections()
{
    vkDestroyPipeline(setup_.ctx_.device_, trace_, nullptr);
    vkDestroyPipeline(setup_.ctx_.device_, resolve_, nullptr);
}

void Reflections::buildPipelines(const ShaderModules& modules)
{
    vkDestroyPipeline(setup_.ctx_.device_, trace_, nullptr);
    vkDestroyPipeline(setup_.ctx_.device_, resolve_, nullptr);
    resolve_ = GraphicsPipelineBuilder()
                   .shaders(modules[SkyVS], modules[ReflectResolvePS])
                   .colorFormat(ReflectRadianceFormat)
                   .build(setup_.ctx_.device_, setup_.layout_);
    trace_ = modules[ReflectTracePS] ? GraphicsPipelineBuilder()
                                           .shaders(modules[SkyVS], modules[ReflectTracePS])
                                           .colorFormat(ReflectRadianceFormat)
                                           .colorFormat(ReflectRayFormat)
                                           .build(setup_.ctx_.device_, setup_.rtLayout_)
                                     : VK_NULL_HANDLE;
}

std::optional<Reflections::Images> Reflections::createImages(RenderGraph& graph,
                                                             VkExtent2D extent,
                                                             const Camera_C& camera,
                                                             bool rayTracing)
{
    images_.reset();
    if (!rayTracing || trace_ == VK_NULL_HANDLE || camera.ortho())
        return images_;
    const GraphImage resolved =
        graph.createImage("reflection resolved", {extent, ReflectRadianceFormat});
    images_ = Images{graph.createImage("reflection surface", {extent, ReflectSurfaceFormat}),
                     graph.createImage("reflection raw", {extent, ReflectRadianceFormat}),
                     graph.createImage("reflection ray", {extent, ReflectRayFormat}), resolved,
                     resolved};
    return images_;
}

void Reflections::addPasses(RenderGraph& graph, GraphImage depth, GraphImage normals,
                            GraphBuffer frameConstants, GraphBuffer uploads,
                            const DrawContext& draw,
                            const std::function<void(GraphPass&)>& readsShading)
{
    if (!images_)
        return;
    GraphPass& trace =
        graph.addPass("reflection trace")
            .read(depth, Usage::ShaderRead)
            .read(normals, Usage::ShaderRead)
            .read(images_->surface_, Usage::ShaderRead)
            .color(images_->raw_, Load::DontCare)
            .color(images_->ray_, Load::DontCare)
            .execute([draw, pipeline = trace_](GraphContext& c)
                     { drawFullscreen(draw, c, pipeline); });
    readsShading(trace);
    graph.addPass("reflection resolve")
        .read(depth, Usage::ShaderRead)
        .read(normals, Usage::ShaderRead)
        .read(images_->surface_, Usage::ShaderRead)
        .read(images_->raw_, Usage::ShaderRead)
        .read(images_->ray_, Usage::ShaderRead)
        .read(frameConstants, Usage::ShaderRead)
        .read(uploads, Usage::AnyRead)
        .color(images_->resolved_, Load::DontCare)
        .execute([draw, pipeline = resolve_](GraphContext& c)
                 { drawFullscreen(draw, c, pipeline); });
}
}  // namespace batap
