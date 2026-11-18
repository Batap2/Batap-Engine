#include "Renderer/Features/Ao/Ao.h"

#include "Components/Camera_C.h"
#include "Renderer/Device/Pipelines.h"
#include "Renderer/Device/VulkanContext.h"

namespace batap
{
namespace
{
constexpr VkFormat AoRawFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr VkFormat AoFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

void dispatchInto(const DrawContext& draw, GraphContext& c, VkPipeline pipeline, GraphImage target)
{
    DrawPush push = draw.push_;
    push.storageImage_ = c.storageIndex(target);
    dispatchScreen(draw, c.cmd(), pipeline, push, c.extent(target));
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
    trace_ = buildComputePipeline(setup_.ctx_.device_, setup_.layout_, modules[AoCS]);
    filter_ = buildComputePipeline(setup_.ctx_.device_, setup_.layout_, modules[AoFilterCS]);
    upsample_ = buildComputePipeline(setup_.ctx_.device_, setup_.layout_, modules[AoUpsampleCS]);
}

std::optional<Ao::Images> Ao::createImages(RenderGraph& graph, VkExtent2D extent,
                                           const Camera_C& settings, bool enabled)
{
    images_.reset();
    if (!enabled || settings.ortho())
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
        .write(images_->raw_, Usage::ComputeWrite)
        .execute([draw, pipeline = trace_, target = images_->raw_](GraphContext& c)
                 { dispatchInto(draw, c, pipeline, target); });
    graph.addPass("ao filter")
        .read(images_->raw_, Usage::ShaderRead)
        .read(depth, Usage::ShaderRead)
        .read(normals, Usage::ShaderRead)
        .read(frameConstants, Usage::ShaderRead)
        .read(uploads, Usage::AnyRead)
        .write(images_->filtered_, Usage::ComputeWrite)
        .execute([draw, pipeline = filter_, target = images_->filtered_](GraphContext& c)
                 { dispatchInto(draw, c, pipeline, target); });
    graph.addPass("ao upsample")
        .read(images_->filtered_, Usage::ShaderRead)
        .read(depth, Usage::ShaderRead)
        .read(normals, Usage::ShaderRead)
        .read(frameConstants, Usage::ShaderRead)
        .read(uploads, Usage::AnyRead)
        .write(images_->full_, Usage::ComputeWrite)
        .execute([draw, pipeline = upsample_, target = images_->full_](GraphContext& c)
                 { dispatchInto(draw, c, pipeline, target); });
}
}  // namespace batap
