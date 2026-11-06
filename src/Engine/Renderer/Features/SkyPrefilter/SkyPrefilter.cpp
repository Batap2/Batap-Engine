#include "Renderer/Features/SkyPrefilter/SkyPrefilter.h"

#include "Renderer/Device/Pipelines.h"
#include "Renderer/Device/VulkanContext.h"

#include <algorithm>
#include <string>

namespace batap
{
namespace
{
void drawFullscreen(const DrawContext& draw, GraphContext& c, VkPipeline pipeline,
                    const DrawPush& push)
{
    const DrawContext bound = bindDraw(draw, c.cmd());
    vkCmdBindPipeline(bound.cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    pushDraw(bound, push);
    vkCmdDraw(bound.cmd_, 3, 1, 0, 0);
}
}  // namespace

SkyPrefilter::SkyPrefilter(const DrawSetup& setup) : setup_(setup) {}

SkyPrefilter::~SkyPrefilter()
{
    vkDestroyPipeline(setup_.ctx_.device_, equirect_, nullptr);
    vkDestroyPipeline(setup_.ctx_.device_, prefilter_, nullptr);
}

void SkyPrefilter::buildPipelines(const ShaderModules& modules)
{
    vkDestroyPipeline(setup_.ctx_.device_, equirect_, nullptr);
    vkDestroyPipeline(setup_.ctx_.device_, prefilter_, nullptr);
    equirect_ = GraphicsPipelineBuilder()
                    .shaders(modules[SkyVS], modules[SkyEquirectPS])
                    .colorFormat(setup_.colorFormat_)
                    .build(setup_.ctx_.device_, setup_.layout_);
    prefilter_ = GraphicsPipelineBuilder()
                     .shaders(modules[SkyVS], modules[SkyPrefilterPS])
                     .colorFormat(setup_.colorFormat_)
                     .build(setup_.ctx_.device_, setup_.layout_);
}

void SkyPrefilter::createImages(RenderGraph& graph, bool hasSky)
{
    levels_.clear();
    if (!hasSky)
        return;
    VkExtent2D level{SkyPrefilterWidth, SkyPrefilterWidth / 2};
    for (uint32_t i = 0; i < SkyPrefilterLevels; ++i)
    {
        levels_.push_back(graph.createImage("sky prefilter " + std::to_string(i),
                                            {level, setup_.colorFormat_}));
        level = {std::max(level.width / 2, 1u), std::max(level.height / 2, 1u)};
    }
}

void SkyPrefilter::addPasses(RenderGraph& graph, const DrawContext& draw,
                             const std::function<void(GraphPass&)>& readsLighting)
{
    if (levels_.empty())
        return;
    GraphPass& equirect = graph.addPass("sky equirect")
                              .color(levels_[0], Load::DontCare)
                              .execute([draw, pipeline = equirect_](GraphContext& c)
                                       { drawFullscreen(draw, c, pipeline, draw.push_); });
    readsLighting(equirect);

    // Each level filters the one before it, already convolved with a narrower
    // lobe: 64 samples then show no noise on a smooth sky, at a slight over-blur.
    for (size_t i = 1; i < levels_.size(); ++i)
    {
        const GraphImage source = levels_[i - 1];
        const float roughness = float(i) / float(SkyPrefilterLevels - 1);
        const uint32_t samples = i < 4 ? 64u : 128u;
        graph.addPass("sky prefilter " + std::to_string(i))
            .read(source, Usage::ShaderRead)
            .color(levels_[i], Load::DontCare)
            .execute(
                [draw, pipeline = prefilter_, source, roughness, samples](GraphContext& c)
                {
                    DrawPush push = draw.push_;
                    push.skySource_ = c.textureIndex(source);
                    push.skyRoughness_ = roughness;
                    push.skySamples_ = samples;
                    drawFullscreen(draw, c, pipeline, push);
                });
    }
}
}  // namespace batap
