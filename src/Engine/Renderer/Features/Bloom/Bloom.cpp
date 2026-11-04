#include "Renderer/Features/Bloom/Bloom.h"

#include "Components/Camera_C.h"
#include "Renderer/Device/VulkanContext.h"

#include <algorithm>
#include <string>

namespace batap
{
namespace
{
constexpr size_t kMaxLevels = 6;
constexpr uint32_t kMinSide = 8;

void drawLevel(const DrawContext& draw, GraphContext& c, VkPipeline pipeline, GraphImage source,
               bool karis, float radius)
{
    const DrawContext bound = bindDraw(draw, c.cmd());
    DrawPush push = bound.push_;
    push.bloomSource_ = c.textureIndex(source);
    push.bloomKaris_ = karis ? 1u : 0u;
    push.bloomRadius_ = radius;
    vkCmdBindPipeline(bound.cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    pushDraw(bound, push);
    vkCmdDraw(bound.cmd_, 3, 1, 0, 0);
}
}  // namespace

Bloom::Bloom(const DrawSetup& setup) : setup_(setup) {}

Bloom::~Bloom()
{
    vkDestroyPipeline(setup_.ctx_.device_, down_, nullptr);
    vkDestroyPipeline(setup_.ctx_.device_, up_, nullptr);
}

void Bloom::buildPipelines(const ShaderModules& modules)
{
    vkDestroyPipeline(setup_.ctx_.device_, down_, nullptr);
    vkDestroyPipeline(setup_.ctx_.device_, up_, nullptr);
    down_ = GraphicsPipelineBuilder()
                .shaders(modules[SkyVS], modules[BloomDownPS])
                .colorFormat(setup_.colorFormat_)
                .build(setup_.ctx_.device_, setup_.layout_);
    up_ =
        GraphicsPipelineBuilder()
            .shaders(modules[SkyVS], modules[BloomUpPS])
            .colorFormat(setup_.colorFormat_, blendState(VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE))
            .build(setup_.ctx_.device_, setup_.layout_);
}

std::optional<GraphImage> Bloom::createImages(RenderGraph& graph, VkExtent2D extent,
                                              const Camera_C& settings)
{
    levels_.clear();
    intensity_ = 0.f;
    radius_ = settings.bloomRadius_;
    if (settings.bloomIntensity_ <= 0.f)
        return std::nullopt;

    VkExtent2D level{extent.width / 2, extent.height / 2};
    while (levels_.size() < kMaxLevels && std::min(level.width, level.height) >= kMinSide)
    {
        levels_.push_back(graph.createImage("bloom " + std::to_string(levels_.size()),
                                            {level, setup_.colorFormat_}));
        level = {level.width / 2, level.height / 2};
    }
    if (levels_.empty())
        return std::nullopt;
    intensity_ = settings.bloomIntensity_;
    return levels_.front();
}

void Bloom::addPasses(RenderGraph& graph, GraphImage hdr, const DrawContext& draw)
{
    if (levels_.empty())
        return;
    for (size_t i = 0; i < levels_.size(); ++i)
    {
        const GraphImage source = i == 0 ? hdr : levels_[i - 1];
        graph.addPass("bloom down " + std::to_string(i))
            .read(source, Usage::ShaderRead)
            .color(levels_[i], Load::DontCare)
            .execute([draw, pipeline = down_, source, karis = i == 0](GraphContext& c)
                     { drawLevel(draw, c, pipeline, source, karis, 1.f); });
    }
    for (size_t i = levels_.size() - 1; i > 0; --i)
    {
        const GraphImage source = levels_[i];
        graph.addPass("bloom up " + std::to_string(i - 1))
            .read(source, Usage::ShaderRead)
            .color(levels_[i - 1], Load::Keep)
            .execute([draw, pipeline = up_, source, radius = radius_](GraphContext& c)
                     { drawLevel(draw, c, pipeline, source, false, radius); });
    }
}
}  // namespace batap
