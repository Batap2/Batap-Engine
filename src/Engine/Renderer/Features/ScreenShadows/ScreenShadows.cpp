#include "Renderer/Features/ScreenShadows/ScreenShadows.h"

#include "Components/Camera_C.h"
#include "Components/DirectionalLight_C.h"
#include "Components/FarPointLight_C.h"
#include "Instance/InstanceManager.h"
#include "Renderer/Device/Pipelines.h"
#include "Renderer/Device/VulkanContext.h"
#include "Systems/CascadeLight_S.h"

namespace batap
{
namespace
{
constexpr VkFormat ScreenShadowFormat = VK_FORMAT_R8_UNORM;
}  // namespace

ScreenShadows::ScreenShadows(const DrawSetup& setup) : setup_(setup) {}

ScreenShadows::~ScreenShadows()
{
    vkDestroyPipeline(setup_.ctx_.device_, pipeline_, nullptr);
}

uint32_t ScreenShadows::lightIndex(entt::registry& reg, GPUInstanceManager& instances)
{
    const entt::entity light = CascadeLight_S::find(reg);
    if (light == entt::null)
        return InvalidGPUIndex;
    const auto* far = reg.try_get<FarPointLight_C>(light);
    const auto* sun = reg.try_get<DirectionalLight_C>(light);
    if (!(far && far->castShadows_) && !(sun && sun->castShadows_))
        return InvalidGPUIndex;
    const GPUInstanceID id = instances.pool<LightInstance>().getGPUIndex(EntityHandle{&reg, light});
    return id.valid() ? static_cast<uint32_t>(id) : InvalidGPUIndex;
}

void ScreenShadows::buildPipelines(const ShaderModules& modules)
{
    vkDestroyPipeline(setup_.ctx_.device_, pipeline_, nullptr);
    pipeline_ = GraphicsPipelineBuilder()
                    .shaders(modules[SkyVS], modules[ScreenShadowPS])
                    .colorFormat(ScreenShadowFormat)
                    .build(setup_.ctx_.device_, setup_.layout_);
}

std::optional<GraphImage> ScreenShadows::createImage(RenderGraph& graph, VkExtent2D extent,
                                                     const Camera_C& settings, bool enabled)
{
    image_.reset();
    if (enabled && !settings.ortho())
        image_ = graph.createImage("screen shadows", {extent, ScreenShadowFormat});
    return image_;
}

void ScreenShadows::addPass(RenderGraph& graph, GraphImage depth, GraphBuffer frameConstants,
                            GraphBuffer uploads, const DrawContext& draw)
{
    if (!image_)
        return;
    graph.addPass("screen shadows")
        .read(depth, Usage::ShaderRead)
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
