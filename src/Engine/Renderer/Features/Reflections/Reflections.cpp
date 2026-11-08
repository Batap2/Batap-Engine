#include "Renderer/Features/Reflections/Reflections.h"

#include "Components/Camera_C.h"
#include "Renderer/Device/Pipelines.h"
#include "Renderer/Device/ResourceManager.h"
#include "Renderer/Device/VulkanContext.h"

namespace batap
{
namespace
{
constexpr VkFormat ReflectRadianceFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr VkFormat ReflectRayFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr VkFormat ReflectHistorySurfaceFormat = VK_FORMAT_R32G32B32A32_UINT;
constexpr VkImageUsageFlags ReflectHistoryUsage =
    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

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
    vkDestroyPipeline(setup_.ctx_.device_, temporal_, nullptr);
    releaseHistory(nullptr);
}

void Reflections::buildPipelines(const ShaderModules& modules)
{
    vkDestroyPipeline(setup_.ctx_.device_, trace_, nullptr);
    vkDestroyPipeline(setup_.ctx_.device_, resolve_, nullptr);
    vkDestroyPipeline(setup_.ctx_.device_, temporal_, nullptr);
    resolve_ = GraphicsPipelineBuilder()
                   .shaders(modules[SkyVS], modules[ReflectResolvePS])
                   .colorFormat(ReflectRadianceFormat)
                   .build(setup_.ctx_.device_, setup_.layout_);
    temporal_ = GraphicsPipelineBuilder()
                    .shaders(modules[SkyVS], modules[ReflectTemporalPS])
                    .colorFormat(ReflectRadianceFormat)
                    .colorFormat(ReflectHistorySurfaceFormat)
                    .build(setup_.ctx_.device_, setup_.layout_);
    trace_ = modules[ReflectTracePS] ? GraphicsPipelineBuilder()
                                           .shaders(modules[SkyVS], modules[ReflectTracePS])
                                           .colorFormat(ReflectRadianceFormat)
                                           .colorFormat(ReflectRayFormat)
                                           .build(setup_.ctx_.device_, setup_.rtLayout_)
                                     : VK_NULL_HANDLE;
}

void Reflections::releaseHistory(RenderGraph* graph)
{
    ResourceManager& resources = setup_.resources_;
    for (std::array<GPUResourceHandle, 2>* images : {&history_, &historySurface_})
        for (GPUResourceHandle& image : *images)
        {
            if (!image.valid())
                continue;
            if (graph)
                graph->forget(resources.imageFor(image));
            resources.requestDestroy(image);
            image = {};
        }
    historyValid_ = false;
}

void Reflections::resizeHistory(RenderGraph& graph, VkExtent2D extent)
{
    releaseHistory(&graph);
    ResourceManager& resources = setup_.resources_;
    for (size_t i = 0; i < history_.size(); ++i)
    {
        history_[i] = resources.createTarget(extent.width, extent.height, ReflectRadianceFormat,
                                             ReflectHistoryUsage);
        historySurface_[i] = resources.createTarget(extent.width, extent.height,
                                                    ReflectHistorySurfaceFormat, ReflectHistoryUsage);
    }
    historyExtent_ = extent;
}

GraphImage Reflections::importHistory(RenderGraph& graph, const char* name,
                                      GPUResourceHandle image)
{
    ResourceManager& resources = setup_.resources_;
    GraphImageInfo info;
    info.image_ = resources.imageFor(image);
    info.view_ = resources.viewFor(image);
    info.extent_ = resources.extentFor(image);
    info.aspect_ = VK_IMAGE_ASPECT_COLOR_BIT;
    info.textureIndex_ = resources.textureIndex(image);
    info.resource_ = image;
    return graph.importImage(name, info, Usage::ShaderRead);
}

std::optional<Reflections::Images> Reflections::createImages(RenderGraph& graph,
                                                             VkExtent2D extent,
                                                             const Camera_C& camera,
                                                             entt::entity cameraEntity,
                                                             bool rayTracing)
{
    images_.reset();
    if (!rayTracing || trace_ == VK_NULL_HANDLE || camera.ortho())
    {
        historyValid_ = false;
        return images_;
    }
    if (extent.width != historyExtent_.width || extent.height != historyExtent_.height)
        resizeHistory(graph, extent);
    if (cameraEntity != historyCamera_)
    {
        historyCamera_ = cameraEntity;
        historyValid_ = false;
    }
    const uint32_t write = historyWrite_;
    const uint32_t read = write ^ 1u;
    Images images;
    images.surface_ = graph.createImage("reflection surface", {extent, ReflectSurfaceFormat});
    images.raw_ = graph.createImage("reflection raw", {extent, ReflectRadianceFormat});
    images.ray_ = graph.createImage("reflection ray", {extent, ReflectRayFormat});
    images.resolved_ = graph.createImage("reflection resolved", {extent, ReflectRadianceFormat});
    images.history_ = importHistory(graph, "reflection history", history_[read]);
    images.historySurface_ =
        importHistory(graph, "reflection history surface", historySurface_[read]);
    images.output_ = importHistory(graph, "reflection output", history_[write]);
    images.outputSurface_ =
        importHistory(graph, "reflection output surface", historySurface_[write]);
    images.historyValid_ = historyValid_;
    images_ = images;
    historyWrite_ = read;
    historyValid_ = true;
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
    GraphPass& temporal = graph.addPass("reflection temporal");
    if (images_->historyValid_)
        temporal.read(images_->history_, Usage::ShaderRead)
            .read(images_->historySurface_, Usage::ShaderRead);
    temporal.read(depth, Usage::ShaderRead)
        .read(normals, Usage::ShaderRead)
        .read(images_->surface_, Usage::ShaderRead)
        .read(images_->resolved_, Usage::ShaderRead)
        .read(frameConstants, Usage::ShaderRead)
        .read(uploads, Usage::AnyRead)
        .color(images_->output_, Load::DontCare)
        .color(images_->outputSurface_, Load::DontCare)
        .execute([draw, pipeline = temporal_](GraphContext& c)
                 { drawFullscreen(draw, c, pipeline); });
}
}  // namespace batap
