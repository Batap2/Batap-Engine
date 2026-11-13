#include "Renderer/Features/ScreenShadows/ScreenShadows.h"

#include "Components/Camera_C.h"
#include "Components/DirectionalLight_C.h"
#include "Components/FarPointLight_C.h"
#include "Components/Transform_C.h"
#include "Instance/InstanceManager.h"
#include "Renderer/Device/Pipelines.h"
#include "Renderer/Device/VulkanContext.h"
#include "Systems/CascadeLight_S.h"

#include <algorithm>
#include <cstring>
#include <span>

namespace batap
{
namespace
{
constexpr VkFormat ScreenShadowFormat = VK_FORMAT_R8_UNORM;
constexpr int kWave = static_cast<int>(ScreenShadowWave);

struct WaveDispatch
{
    std::array<int, 3> count_{};
    std::array<int, 2> offset_{};
};

uint32_t buildDispatches(const v4f& clip, int width, int height, v4f& light,
                         std::array<WaveDispatch, ScreenShadowDispatches>& out)
{
    const float limit = 0.000002f * static_cast<float>(kWave);
    float w = clip.w();
    if (w >= 0.f && w < limit)
        w = limit;
    else if (w < 0.f && w > -limit)
        w = -limit;
    light.x() = (clip.x() / w * 0.5f + 0.5f) * static_cast<float>(width);
    light.y() = (clip.y() / w * -0.5f + 0.5f) * static_cast<float>(height);
    light.z() = clip.w() == 0.f ? 0.f : clip.z() / clip.w();
    light.w() = clip.w() > 0.f ? 1.f : -1.f;

    const int lightX = static_cast<int>(light.x() + 0.5f);
    const int lightY = static_cast<int>(light.y() + 0.5f);
    const std::array<int, 4> biased{-lightX, -(height - lightY), width - lightX, lightY};

    uint32_t count = 0;
    for (int q = 0; q < 4; ++q)
    {
        const bool vertical = q == 0 || q == 3;
        const std::array<int, 4> bounds{
            std::max(0, ((q & 1) ? biased[0] : -biased[2])) / kWave,
            std::max(0, ((q & 2) ? biased[1] : -biased[3])) / kWave,
            std::max(0, ((q & 1) ? biased[2] : -biased[0]) + kWave * (vertical ? 1 : 2) - 1) /
                kWave,
            std::max(0, ((q & 2) ? biased[3] : -biased[1]) + kWave * (vertical ? 2 : 1) - 1) /
                kWave,
        };
        if (bounds[2] - bounds[0] <= 0 || bounds[3] - bounds[1] <= 0)
            continue;

        WaveDispatch& d = out[count++];
        d.count_ = {kWave, bounds[2] - bounds[0], bounds[3] - bounds[1]};
        d.offset_ = {((q & 1) ? bounds[0] : -bounds[2]) + ((q == 2 || q == 3) ? 1 : 0),
                     ((q & 2) ? -bounds[3] : bounds[1]) + ((q == 1 || q == 3) ? 1 : 0)};

        int axisDelta = biased[0] - biased[1];
        if (q == 1)
            axisDelta = biased[2] + biased[1];
        if (q == 2)
            axisDelta = -biased[0] - biased[3];
        if (q == 3)
            axisDelta = -biased[2] + biased[3];
        axisDelta = (axisDelta + kWave - 1) / kWave;
        if (axisDelta <= 0)
            continue;

        WaveDispatch& d2 = out[count++];
        d2 = d;
        if (q == 0)
        {
            d2.count_[2] = std::min(d.count_[2], axisDelta);
            d.count_[2] -= d2.count_[2];
            d2.offset_[1] = d.offset_[1] + d.count_[2];
            --d2.offset_[0];
            ++d2.count_[1];
        }
        if (q == 1)
        {
            d2.count_[1] = std::min(d.count_[1], axisDelta);
            d.count_[1] -= d2.count_[1];
            d2.offset_[0] = d.offset_[0] + d.count_[1];
            ++d2.count_[2];
        }
        if (q == 2)
        {
            d2.count_[1] = std::min(d.count_[1], axisDelta);
            d.count_[1] -= d2.count_[1];
            d.offset_[0] += d2.count_[1];
            ++d2.count_[2];
            --d2.offset_[1];
        }
        if (q == 3)
        {
            d2.count_[2] = std::min(d.count_[2], axisDelta);
            d.count_[2] -= d2.count_[2];
            d.offset_[1] += d2.count_[2];
            ++d2.count_[1];
        }
        if (d2.count_[1] <= 0 || d2.count_[2] <= 0)
            d2 = out[--count];
        if (d.count_[1] <= 0 || d.count_[2] <= 0)
            d = out[--count];
    }
    for (uint32_t i = 0; i < count; ++i)
        for (int& offset : out[i].offset_)
            offset *= kWave;
    return count;
}
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
    pipeline_ = buildComputePipeline(setup_.ctx_.device_, setup_.layout_, modules[ScreenShadowCS]);
}

bool ScreenShadows::plan(const entt::registry& reg, entt::entity camera, VkExtent2D extent,
                         float aspect, FrameGPUData& frame)
{
    dispatchCount_ = 0;
    const entt::entity light = CascadeLight_S::find(reg);
    const auto* lightTransform = light == entt::null ? nullptr : reg.try_get<Transform_C>(light);
    const auto* camTransform = reg.try_get<Transform_C>(camera);
    const auto* cam = reg.try_get<Camera_C>(camera);
    if (!lightTransform || !camTransform || !cam || extent.width == 0 || extent.height == 0)
        return false;

    const transform& camWorld = camTransform->world();
    const transform& lightWorld = lightTransform->world();
    v4f toLight{0.f, 0.f, 0.f, 1.f};
    if (reg.all_of<DirectionalLight_C>(light))
        toLight << lightWorld.linear().col(2).normalized(), 0.f;
    else
        toLight.head<3>() = lightWorld.translation() - camWorld.translation();
    const m4f view = cam->make_view(camWorld);
    v4f viewSpace = toLight;
    viewSpace.head<3>() = view.topLeftCorner<3, 3>() * toLight.head<3>();
    const v4f clip = cam->make_proj(aspect) * viewSpace;

    std::array<WaveDispatch, ScreenShadowDispatches> waves{};
    v4f lightCoord;
    dispatchCount_ = buildDispatches(clip, static_cast<int>(extent.width),
                                     static_cast<int>(extent.height), lightCoord, waves);
    std::copy_n(lightCoord.data(), 4, frame.screenShadowLightCoord_);
    const std::span<int4> waveOffsets{frame.screenShadowWaves_};
    for (uint32_t i = 0; i < dispatchCount_; ++i)
    {
        const std::array<int32_t, 4> offset{waves[i].offset_[0], waves[i].offset_[1], 0, 0};
        std::memcpy(waveOffsets[i], offset.data(), sizeof offset);
        for (size_t k = 0; k < 3; ++k)
            dispatches_[i][k] = static_cast<uint32_t>(waves[i].count_[k]);
    }
    return dispatchCount_ > 0;
}

std::optional<GraphImage> ScreenShadows::createImage(RenderGraph& graph, VkExtent2D extent,
                                                     const Camera_C& settings, bool enabled)
{
    image_.reset();
    if (enabled && !settings.ortho() && dispatchCount_ > 0)
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
        .write(*image_, Usage::ComputeWrite)
        .execute(
            [draw, pipeline = pipeline_, image = *image_, dispatches = dispatches_,
             count = dispatchCount_](GraphContext& c)
            {
                const DrawContext bound = bindCompute(draw, c.cmd());
                vkCmdBindPipeline(bound.cmd_, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
                DrawPush push = bound.push_;
                push.submeshIndex_ = c.storageIndex(image);
                for (uint32_t i = 0; i < count; ++i)
                {
                    push.instanceIndex_ = i;
                    pushDraw(bound, push);
                    vkCmdDispatch(bound.cmd_, dispatches[i][0], dispatches[i][1],
                                  dispatches[i][2]);
                }
            });
}
}  // namespace batap
