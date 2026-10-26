#include "Renderer/Scene/SceneFrame.h"

#include "Assets/AssetManager.h"
#include "Components/Camera_C.h"
#include "Components/Transform_C.h"
#include "DebugUtils.h"
#include "Engine.h"
#include "Instance/InstanceManager.h"
#include "Renderer/Device/Formats.h"
#include "Renderer/Device/ResourceManager.h"
#include "Renderer/Device/VulkanContext.h"
#include "Renderer/Scene/LtcTables.h"
#include "Renderer/Tonemap.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace batap
{
namespace
{
constexpr ResourceFormat HdrFormat = ResourceFormat::R16G16B16A16_FLOAT;

template <class T, class... Args>
std::optional<T> whenRayTracing(const VulkanContext& ctx, Args&... args)
{
    if (!ctx.rtSupported_)
        return std::nullopt;
    return std::optional<T>(std::in_place, args...);
}
}  // namespace

SceneFrame::SceneFrame(VulkanContext& ctx, ResourceManager& resources, VkFormat outputFormat,
                       VkFormat depthFormat)
    : resources_(resources),
      depthFormat_(depthFormat),
      shaders_(ctx),
      bindings_(ctx, resources),
      accelerationStructures_(whenRayTracing<AccelerationStructures>(ctx, ctx, resources)),
      giCache_(whenRayTracing<GiCache>(ctx, resources)),
      shadows_(bindings_.drawSetup(toVkFormat(HdrFormat), depthFormat)),
      meshes_(bindings_.drawSetup(toVkFormat(HdrFormat), depthFormat)),
      terrain_(bindings_.drawSetup(toVkFormat(HdrFormat), depthFormat)),
      billboards_(bindings_.drawSetup(toVkFormat(HdrFormat), depthFormat)),
      fullscreen_(bindings_.drawSetup(toVkFormat(HdrFormat), depthFormat), outputFormat),
      debug_(bindings_.drawSetup(outputFormat, depthFormat)),
      giUpdate_(bindings_.drawSetup(toVkFormat(HdrFormat), depthFormat)),
      exposure_(bindings_.drawSetup(outputFormat, depthFormat))
{
    const auto uploadTable =
        [&](const std::array<uint16_t, LtcTableTexels * 4>& table, const char* name)
    {
        const GPUResourceHandle image = resources_.createImage2D(
            LtcTableSize, LtcTableSize, ResourceFormat::R16G16B16A16_FLOAT, name);
        auto span = resources_.requestTextureUpload(image, LtcTableSize, LtcTableSize,
                                                    ResourceFormat::R16G16B16A16_FLOAT);
        std::memcpy(span.data(), table.data(), sizeof(table));
        return image;
    };
    ltcMat_ = uploadTable(LtcMatTable, "ltc matrix table");
    ltcAmp_ = uploadTable(LtcAmpTable, "ltc amplitude table");

    frameData_.ltcMatTexture_ = resources_.textureIndex(ltcMat_);
    frameData_.ltcAmpTexture_ = resources_.textureIndex(ltcAmp_);
    frameConstants_ = resources_.createPerFrameBuffer(sizeof(FrameGPUData), "frame constants");

    buildPipelines(shaders_.modules());
}

void SceneFrame::buildPipelines(const ShaderModules& modules)
{
    shadows_.buildPipelines(modules);
    meshes_.buildPipelines(modules);
    terrain_.buildPipelines(modules);
    billboards_.buildPipelines(modules);
    fullscreen_.buildPipelines(modules);
    debug_.buildPipelines(modules);
    giUpdate_.buildPipelines(modules);
    exposure_.buildPipelines(modules);
}

void SceneFrame::uploadDebugDraw(const DebugDraw& depthTested, const DebugDraw& overlay)
{
    debug_.upload(depthTested, overlay);
}

void SceneFrame::uploadBillboards(const Billboards& billboards)
{
    billboards_.upload(billboards);
}

void SceneFrame::uploadTerrain(TerrainDraws& draws)
{
    terrain_.upload(draws);
}

void SceneFrame::checkHotReload()
{
    if (shaders_.reloadIfChanged())
        buildPipelines(shaders_.modules());
}

void SceneFrame::writeFrameSet(uint32_t frame, const SceneRenderArgs& args, Engine& ctx)
{
    std::array<VkBuffer, FrameSetBindingCount> buffers{};
    auto claim = [&](uint32_t binding, VkBuffer buffer)
    {
        if (binding >= FrameSetBindingCount || buffers[binding] != VK_NULL_HANDLE)
            ThrowRuntime("frame set: two writers claim the same binding");
        buffers[binding] = buffer;
    };

    args.instanceManager_->forEachPool(
        [&](auto& pool)
        {
            using InstanceT = typename std::remove_reference_t<decltype(pool)>::InstanceType;
            claim(InstanceT::Binding, resources_.bufferFor(pool.instancePoolHandle_));
        });
    claim(MaterialsBinding,
          resources_.bufferFor(ctx.assetManager_->getGPUArena<Material>()->bufferHandle()));
    claim(DebugShapeVertsBinding, resources_.bufferFor(debug_.vertsBuffer()));
    claim(DebugShapesBinding, resources_.bufferFor(debug_.shapesBuffer()));
    claim(BillboardsBinding, resources_.bufferFor(billboards_.buffer()));
    claim(ShadowsBinding, resources_.bufferFor(shadows_.buffer()));
    claim(FrameConstantsBinding, resources_.bufferFor(frameConstants_));
    claim(TerrainChunksBinding, resources_.bufferFor(terrain_.chunkBuffer()));
    claim(TerrainVerticesBinding, resources_.bufferFor(terrain_.vertexBuffer()));
    claim(TerrainWorldsBinding, resources_.bufferFor(terrain_.worldBuffer()));

    bindings_.writeFrameSet(frame, buffers);
}

DrawContext SceneFrame::drawContext(uint32_t frame, uint32_t camera, const SceneRenderArgs& args,
                                    Engine& ctx, bool rayTracing) const
{
    DrawContext draw = bindings_.drawContext(frame, rayTracing);
    draw.push_.cameraIndex_ = camera;
    draw.push_.debugFlags_ =
        (args.showShadowCascades_ ? DebugShadowCascades : 0u) |
        (args.showRtFirstHit_ ? DebugRtFirstHit : 0u) | (args.showGiCells_ ? DebugGiCells : 0u) |
        (args.showGiSides_ ? DebugGiSides : 0u) | (args.showGiCache_ ? DebugGiCache : 0u);
    draw.reg_ = args.reg_;
    draw.instanceManager_ = args.instanceManager_;
    draw.assetManager_ = ctx.assetManager_.get();
    args.instanceManager_->forEachPool(
        [&](auto& pool)
        {
            using InstanceT = typename std::remove_reference_t<decltype(pool)>::InstanceType;
            if constexpr (requires { InstanceT::CountField; })
                draw.push_.*InstanceT::CountField = static_cast<uint32_t>(pool.size());
        });
    return draw;
}

bool SceneFrame::addPasses(RenderGraph& graph, uint32_t frame, const RenderTargets& targets,
                           GraphBuffer uploads, const SceneRenderArgs& args, Engine& ctx)
{
    entt::registry* reg = args.reg_;
    GPUInstanceManager* instances = args.instanceManager_;
    if (!reg)
        return false;

    const EntityHandle cam{reg, args.camera_};
    if (!cam.valid() || !reg->all_of<Camera_C, Transform_C>(args.camera_))
        return false;
    const auto camID = instances->pool<CameraInstance>().getGPUIndex(cam);
    if (!camID.valid())
        return false;

    const bool rayTracing = accelerationStructures_ && ctx.settings().rayTracing_;
    AssetManager& assets = *ctx.assetManager_;

    shadows_.prepare(*reg, *instances, args.camera_, ctx.getFrameSize(), rayTracing);

    AccelerationStructures::Buffers as;
    GiCache::Buffers gi;
    if (rayTracing)
    {
        as = accelerationStructures_->addPasses(graph, uploads, frame, *reg, *instances, assets,
                                                terrain_, args.camera_);
        gi = giCache_->addPasses(graph);
        bindings_.writeRayTracingSet(frame, *accelerationStructures_, *giCache_);
    }

    const float dt = std::clamp(ctx.deltaTime_, 1.f / 4096.f, 1.f / 8.f);
    frameData_.tonemap_ = tonemapChoice(ctx.settings().tonemap_).operator_;
    frameData_.exposure_ =
        exposure_.update(frame, args.camera_, reg->get<Camera_C>(args.camera_), dt);
    giUpdate_.writeFrameData(frameData_, targets.extent_, dt, ctx.settings().giBounces_,
                             ctx.settings().giPasses_);

    writeFrameSet(frame, args, ctx);
    const DrawContext draw = drawContext(frame, camID, args, ctx, rayTracing);

    const GraphImage hdr = graph.createImage("hdr", {targets.extent_, toVkFormat(HdrFormat)});
    const GraphImage depth = graph.createImage("depth", {targets.extent_, depthFormat_});

    const GraphBuffer frameConstants = graph.importBuffer(
        "frame constants", resources_, frameConstants_, Usage::None, Usage::None);
    graph.addPass("frame constants")
        .write(frameConstants, Usage::TransferDst)
        .execute(
            [frameConstants, hdr, data = frameData_](GraphContext& c) mutable
            {
                data.hdrTexture_ = c.graph_.textureIndex(hdr);
                vkCmdUpdateBuffer(c.cmd(), c.buffer(frameConstants), 0, sizeof data, &data);
            });

    if (rayTracing)
    {
        giUpdate_.addPasses(graph, as.tlas_, as.blases_, gi, uploads, frameConstants, draw, frame,
                            targets.extent_);
        giUpdate_.report(giCache_->addStatsPass(graph, gi, frame), args.showGiCache_);
    }

    const Shadows::Output shadows = shadows_.addPasses(graph, uploads, draw, terrain_);

    const bool rtFirstHit = rayTracing && (draw.push_.debugFlags_ & DebugRtFirstHit) != 0u;
    GraphPass& scene = graph.addPass("scene")
                           .color(hdr, Load::Clear, targets.clearColor_)
                           // Reversed Z (Camera_C::make_proj): the far plane is 0.
                           .depth(depth, Load::Clear, 0.f)
                           .read(uploads, Usage::AnyRead)
                           .read(frameConstants, Usage::ShaderRead)
                           .read(shadows.atlases_[0], Usage::ShaderRead)
                           .read(shadows.atlases_[1], Usage::ShaderRead)
                           .execute(
                               [this, draw, rtFirstHit](GraphContext& c)
                               {
                                   const DrawContext bound = bindDraw(draw, c.cmd());
                                   if (rtFirstHit)
                                   {
                                       fullscreen_.recordRtFirstHit(bound);
                                       return;
                                   }
                                   meshes_.record(bound);
                                   terrain_.record(bound);
                                   billboards_.record(bound);
                                   fullscreen_.recordSky(bound);
                               });
    if (shadows.hasEntries_)
        scene.read(shadows.shadows_, Usage::ShaderRead);
    if (rayTracing)
        scene.read(gi.keys_, Usage::ShaderRead)
            .read(gi.values_, Usage::ShaderRead)
            .read(as.tlas_, Usage::AccelerationStructureRead)
            .read(as.blases_, Usage::AccelerationStructureRead);

    exposure_.addPasses(graph, hdr, frameConstants, draw, frame);

    graph.addPass("tonemap")
        .color(targets.color_, Load::DontCare)
        .depth(depth, Load::Keep)
        .read(uploads, Usage::AnyRead)
        .read(hdr, Usage::ShaderRead)
        .read(frameConstants, Usage::ShaderRead)
        .execute(
            [this, draw](GraphContext& c)
            {
                const DrawContext bound = bindDraw(draw, c.cmd());
                fullscreen_.recordTonemap(bound);
                debug_.record(bound);
            });
    return true;
}
}  // namespace batap
