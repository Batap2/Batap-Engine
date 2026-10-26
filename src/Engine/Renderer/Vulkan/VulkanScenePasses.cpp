#include "VulkanScenePasses.h"

#include "RenderGraph.h"
#include "VulkanContext.h"
#include "VulkanFormats.h"
#include "VulkanPipelines.h"
#include "VulkanResources.h"

#include "Assets/AssetManager.h"
#include "Components/Camera_C.h"
#include "Components/DirectionalLight_C.h"
#include "Components/FarPointLight_C.h"
#include "Components/Name_C.h"
#include "Components/Transform_C.h"
#include "DebugUtils.h"
#include "Engine.h"
#include "Instance/InstanceManager.h"
#include "Paths.h"
#include "Renderer/LtcTables.h"
#include "Renderer/ShadowCascades.h"
#include "Renderer/Tonemap.h"
#include "Systems/CascadeLight_S.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>

namespace batap
{
namespace
{
constexpr ResourceFormat HdrFormat = ResourceFormat::R16G16B16A16_FLOAT;

VkDescriptorSetLayout createFrameSetLayout(VkDevice device)
{
    std::array<VkDescriptorSetLayoutBinding, FrameSetBindingCount> bindings{};
    for (uint32_t i = 0; i < FrameSetBindingCount; ++i)
    {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = FrameSetBindingCount;
    layoutInfo.pBindings = bindings.data();

    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &layout) != VK_SUCCESS)
        throw std::runtime_error("ScenePasses : frame set layout");
    return layout;
}

VkDescriptorPool createFramePool(VkDevice device)
{
    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                  FrameSetBindingCount * FramesInFlight};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = FramesInFlight;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;

    VkDescriptorPool pool = VK_NULL_HANDLE;
    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool) != VK_SUCCESS)
        throw std::runtime_error("ScenePasses : frame pool");
    return pool;
}

std::vector<VkDescriptorSet> allocFrameSets(VkDevice device, VkDescriptorPool pool,
                                            VkDescriptorSetLayout layout)
{
    std::vector<VkDescriptorSetLayout> layouts(FramesInFlight, layout);
    VkDescriptorSetAllocateInfo setInfo{};
    setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    setInfo.descriptorPool = pool;
    setInfo.descriptorSetCount = FramesInFlight;
    setInfo.pSetLayouts = layouts.data();

    std::vector<VkDescriptorSet> sets(FramesInFlight);
    if (vkAllocateDescriptorSets(device, &setInfo, sets.data()) != VK_SUCCESS)
        throw std::runtime_error("ScenePasses : frame sets");
    return sets;
}

std::optional<RtScene> createRtScene(VulkanContext& ctx, ResourceManager& resources)
{
    if (!ctx.rtSupported_)
        return std::nullopt;
    return std::optional<RtScene>(std::in_place, ctx, resources);
}

VkPipelineLayout createPipelineLayout(VkDevice device,
                                      std::span<const VkDescriptorSetLayout> setLayouts)
{
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.size = sizeof(DrawPush);

    VkPipelineLayoutCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    info.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
    info.pSetLayouts = setLayouts.data();
    info.pushConstantRangeCount = 1;
    info.pPushConstantRanges = &pushRange;

    VkPipelineLayout layout = VK_NULL_HANDLE;
    if (vkCreatePipelineLayout(device, &info, nullptr, &layout) != VK_SUCCESS)
        throw std::runtime_error("ScenePasses : pipeline layout");
    return layout;
}

}  // namespace

// Declaration order matters: a pass captures the pipeline layout by value, so
// the layout must already be built when the passes are initialised.
ScenePasses::ScenePasses(VulkanContext& ctx, ResourceManager& resources, VkFormat outputFormat,
                         VkFormat depthFormat)
    : ctx_(ctx),
      resources_(resources),
      frameSetLayout_(createFrameSetLayout(ctx.device_)),
      framePool_(createFramePool(ctx.device_)),
      frameSets_(allocFrameSets(ctx.device_, framePool_, frameSetLayout_)),
      pipelineLayout_(createPipelineLayout(
          ctx.device_, std::array{resources.textureSetLayout(), frameSetLayout_})),
      rtScene_(createRtScene(ctx, resources)),
      rtPipelineLayout_(rtScene_
                            ? createPipelineLayout(
                                  ctx.device_, std::array{resources.textureSetLayout(),
                                                          frameSetLayout_, rtScene_->setLayout()})
                            : VK_NULL_HANDLE),
      shadow_(PassSetup{ctx, resources, pipelineLayout_, toVkFormat(HdrFormat), depthFormat,
                        rtPipelineLayout_}),
      geometry_(PassSetup{ctx, resources, pipelineLayout_, toVkFormat(HdrFormat), depthFormat,
                          rtPipelineLayout_}),
      terrain_(PassSetup{ctx, resources, pipelineLayout_, toVkFormat(HdrFormat), depthFormat,
                         rtPipelineLayout_}),
      billboards_(PassSetup{ctx, resources, pipelineLayout_, toVkFormat(HdrFormat), depthFormat,
                            rtPipelineLayout_}),
      sky_(PassSetup{ctx, resources, pipelineLayout_, toVkFormat(HdrFormat), depthFormat,
                     rtPipelineLayout_}),
      debug_(
          PassSetup{ctx, resources, pipelineLayout_, outputFormat, depthFormat, rtPipelineLayout_}),
      rtDebug_(PassSetup{ctx, resources, pipelineLayout_, toVkFormat(HdrFormat), depthFormat,
                         rtPipelineLayout_}),
      giUpdate_(PassSetup{ctx, resources, pipelineLayout_, toVkFormat(HdrFormat), depthFormat,
                          rtPipelineLayout_}),
      tonemap_(
          PassSetup{ctx, resources, pipelineLayout_, outputFormat, depthFormat, rtPipelineLayout_}),
      exposure_(
          PassSetup{ctx, resources, pipelineLayout_, outputFormat, depthFormat, rtPipelineLayout_})
{
    localAtlas_ = resources_.createTarget(LocalAtlasSize, LocalAtlasSize, ResourceFormat::D32_FLOAT,
                                          "local shadow atlas");
    cascadeAtlas_ = resources_.createTarget(CascadeAtlasSize, CascadeAtlasSize,
                                            ResourceFormat::D32_FLOAT, "cascade shadow atlas");

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

    const std::string shaderDir = resolveEngineFile("shaders", "shaders");

    std::array<std::optional<ShaderModule>, ShaderCount> owned;
    ShaderModules modules{};
    for (size_t i = 0; i < ShaderCount; ++i)
    {
        if (kShaders[i].rayTracing_ && !ctx_.rtSupported_)
            continue;
        owned[i].emplace(ctx_.device_, shaderDir + "/" + kShaders[i].name_ + ".spv");
        modules[i] = *owned[i];
    }
    buildPipelines(modules);
}

ScenePasses::~ScenePasses()
{
    vkDestroyPipelineLayout(ctx_.device_, rtPipelineLayout_, nullptr);
    vkDestroyPipelineLayout(ctx_.device_, pipelineLayout_, nullptr);
    vkDestroyDescriptorPool(ctx_.device_, framePool_, nullptr);
    vkDestroyDescriptorSetLayout(ctx_.device_, frameSetLayout_, nullptr);
}

void ScenePasses::buildPipelines(const ShaderModules& modules)
{
    shadow_.buildPipelines(modules);
    geometry_.buildPipelines(modules);
    terrain_.buildPipelines(modules);
    billboards_.buildPipelines(modules);
    sky_.buildPipelines(modules);
    debug_.buildPipelines(modules);
    rtDebug_.buildPipelines(modules);
    giUpdate_.buildPipelines(modules);
    tonemap_.buildPipelines(modules);
    exposure_.buildPipelines(modules);
}

void ScenePasses::uploadDebugDraw(const DebugDraw& depthTested, const DebugDraw& overlay)
{
    debug_.upload(depthTested, overlay);
}

void ScenePasses::uploadBillboards(const Billboards& billboards)
{
    billboards_.upload(billboards);
}

void ScenePasses::uploadTerrain(TerrainDraws& draws)
{
    terrain_.upload(draws);
}

void ScenePasses::checkHotReload()
{
    namespace fs = std::filesystem;

    // Les sources HLSL de l'arbre (pas les .spv du build) : le hot reload est
    // un outil de dev, il vit la ou on edite.
    const fs::path sourceDir = fs::path(BATAP_ROOT_DIR) / "src/Engine/Shaders";

    // Tout le dossier : un header partage declenche le reload comme une source.
    fs::file_time_type latest{};
    std::error_code ec;
    for (const fs::directory_entry& entry : fs::directory_iterator(sourceDir, ec))
        if (entry.is_regular_file(ec))
            latest = std::max(latest, entry.last_write_time(ec));

    if (shadersMtime_ == fs::file_time_type{})
    {
        shadersMtime_ = latest;  // baseline au premier appel, pas de rebuild
        return;
    }
    if (latest <= shadersMtime_)
        return;
    shadersMtime_ = latest;  // meme en cas d'echec : on retentera a la
                             // prochaine sauvegarde, pas a chaque check

    std::array<std::vector<uint8_t>, ShaderCount> spirv;
    for (size_t i = 0; i < ShaderCount; ++i)
    {
        if (kShaders[i].rayTracing_ && !ctx_.rtSupported_)
            continue;
        const std::string source =
            (sourceDir / (std::string(kShaders[i].source_) + ".hlsl")).string();
        spirv[i] = ctx_.shaderCompiler_.compile(
            source, kShaders[i].target_, kShaders[i].rayTracing_ ? kRayTracingDefine : nullptr);
        if (spirv[i].empty())
        {
            std::cerr << "[ShaderCompiler] " << kShaders[i].name_
                      << " : compilation echouee, pipelines conservees\n";
            return;
        }
    }

    vkDeviceWaitIdle(ctx_.device_);
    std::array<std::optional<ShaderModule>, ShaderCount> owned;
    ShaderModules modules{};
    for (size_t i = 0; i < ShaderCount; ++i)
    {
        if (spirv[i].empty())
            continue;
        owned[i].emplace(ctx_.device_, spirv[i].data(), spirv[i].size());
        modules[i] = *owned[i];
    }
    buildPipelines(modules);
    std::cout << "[ShaderCompiler] shaders reloaded" << std::endl;
}

void ScenePasses::writeFrameSet(uint32_t frame, const SceneRenderArgs& args, Engine& ctx)
{
    auto* instanceM = args.instanceManager_;

    // Every binding must be claimed exactly once: written twice one of the two
    // buffers is silently lost, never written the shader reads a null buffer.
    // No driver reports either, so this is the only place it can be caught.
    std::array<VkBuffer, FrameSetBindingCount> buffers{};
    auto claim = [&](uint32_t binding, VkBuffer buffer)
    {
        if (binding >= FrameSetBindingCount || buffers[binding] != VK_NULL_HANDLE)
            ThrowRuntime("frame set: two writers claim the same binding");
        buffers[binding] = buffer;
    };

    instanceM->forEachPool(
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
    claim(ShadowsBinding, resources_.bufferFor(shadow_.buffer()));
    claim(FrameConstantsBinding, resources_.bufferFor(frameConstants_));
    claim(TerrainChunksBinding, resources_.bufferFor(terrain_.chunkBuffer()));
    claim(TerrainVerticesBinding, resources_.bufferFor(terrain_.vertexBuffer()));
    claim(TerrainWorldsBinding, resources_.bufferFor(terrain_.worldBuffer()));

    std::array<VkDescriptorBufferInfo, FrameSetBindingCount> bufferInfos{};
    std::array<VkWriteDescriptorSet, FrameSetBindingCount> writes{};
    for (uint32_t i = 0; i < FrameSetBindingCount; ++i)
    {
        if (buffers[i] == VK_NULL_HANDLE)
            ThrowRuntime("frame set: a binding has no buffer behind it");

        bufferInfos[i].buffer = buffers[i];
        bufferInfos[i].range = VK_WHOLE_SIZE;

        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = frameSets_[frame];
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[i].pBufferInfo = &bufferInfos[i];
    }
    vkUpdateDescriptorSets(ctx_.device_, FrameSetBindingCount, writes.data(), 0, nullptr);
}

void ScenePasses::fitCascades(entt::registry& reg, GPUInstanceManager& instances,
                              entt::entity camera, v2i frameSize)
{
    cascadeViews_.clear();
    const entt::entity light = CascadeLight_S::find(reg);
    const auto* far = light == entt::null ? nullptr : reg.try_get<FarPointLight_C>(light);
    const auto* sun = light == entt::null ? nullptr : reg.try_get<DirectionalLight_C>(light);
    const auto* lightTrans = light == entt::null ? nullptr : reg.try_get<Transform_C>(light);
    const bool casts = (far && far->castShadows_) || (sun && sun->castShadows_);
    if (!casts || !lightTrans || frameSize.y() <= 0)
        return;
    const GPUInstanceID id = instances.pool<LightInstance>().getGPUIndex(EntityHandle{&reg, light});
    if (!id.valid())
        return;

    const transform camWorld = reg.get<Transform_C>(camera).world();
    // The render camera's position as the GPU gets it (CameraGPUData::pos_).
    const v3d origin = camWorld.translation().cast<double>();
    ShadowFitInput in;
    in.camPos_ = camWorld.translation();
    in.camFwd_ = -camWorld.linear().col(2).normalized();
    in.fovY_ = reg.get<Camera_C>(camera).fov_;
    in.aspect_ = static_cast<float>(frameSize.x()) / static_cast<float>(frameSize.y());
    in.range_ = CascadeRange;
    in.lightPos_ = lightTrans->world().translation();
    if (sun)
        in.lightDir_ = -lightTrans->world().linear().col(2).normalized();
    const ShadowFit fit = fitShadowCascades(in);

    uint32_t tooClose = 0;
    for (uint32_t c = 0; c < fit.count_; ++c)
    {
        const CascadeSphere& s = fit.cascades_[c];
        if (s.lightDistance_ < CascadeLightMinRadii)
            tooClose |= 1u << c;
        if (s.radius_ <= 0.f)
            continue;

        ShadowView& view = cascadeViews_.emplace_back();
        view.viewProj_ = cascadeViewProj(s, origin);
        const v3f centre = (s.center_.cast<double>() - origin).cast<float>();
        view.sphere_ = v4f{centre.x(), centre.y(), centre.z(), s.radius_};
        view.texelWorld_ = s.texelWorld_;
        view.x_ = (c % 2) * CascadeTileSize;
        view.y_ = (c / 2) * CascadeTileSize;
        view.size_ = CascadeTileSize;
        view.entry_ = id * MaxShadowViewsPerLight + c;
    }

    if (tooClose != 0 && tooClose != warnedCascades_)
    {
        const auto* name = reg.try_get<Name_C>(light);
        std::cerr << "[Shadows] FarPointLight_C '" << (name ? name->name_ : std::string{})
                  << "' is too close to the cascades it drives: a positional light needs "
                  << CascadeLightMinRadii << " radii of a cascade for its shadows to hold.";
        for (uint32_t c = 0; c < fit.count_; ++c)
            if (tooClose & (1u << c))
                std::cerr << " Cascade " << c << ": " << fit.cascades_[c].lightDistance_ << " radii"
                          << (fit.cascades_[c].radius_ <= 0.f ? ", inside, off." : ".");
        std::cerr << "\n";
    }
    warnedCascades_ = tooClose;
}

bool ScenePasses::addPasses(RenderGraph& graph, uint32_t frame, const RenderTargets& targets,
                            GraphBuffer uploads, const SceneRenderArgs& args, Engine& ctx)
{
    auto* reg = args.reg_;
    auto* instanceM = args.instanceManager_;
    if (!reg)
        return false;

    const EntityHandle cam{reg, args.camera_};
    if (!cam.valid() || !reg->all_of<Camera_C, Transform_C>(args.camera_))
        return false;
    const auto camID = instanceM->pool<CameraInstance>().getGPUIndex(cam);
    if (!camID.valid())
        return false;

    const bool rayTracing = rtScene_ && ctx.settings().rayTracing_;

    localShadowsAlloc_.allocate(*reg, *instanceM, args.camera_, ctx.getFrameSize(), rayTracing,
                                shadowViews_);
    if (rayTracing)
        cascadeViews_.clear();
    else
        fitCascades(*reg, *instanceM, args.camera_, ctx.getFrameSize());
    const size_t shadowEntries = instanceM->pool<LightInstance>().size() * MaxShadowViewsPerLight;
    shadow_.reserve(shadowEntries);

    RtSceneBuffers rt;
    if (rayTracing)
    {
        rt = rtScene_->addPasses(graph, uploads, frame, *reg, *instanceM, *ctx.assetManager_,
                                 terrain_, args.camera_);
        giUpdate_.resize(ctx.getFrameSize());
    }

    if (!hdrTarget_.valid() || hdrExtent_.width != targets.extent_.width ||
        hdrExtent_.height != targets.extent_.height)
    {
        if (hdrTarget_.valid())
            resources_.requestDestroy(hdrTarget_);
        hdrTarget_ = resources_.createTarget(targets.extent_.width, targets.extent_.height,
                                             HdrFormat, "hdr color");
        hdrExtent_ = targets.extent_;
    }

    frameData_.hdrTexture_ = resources_.textureIndex(hdrTarget_);
    const float dt = std::clamp(ctx.deltaTime_, 1.f / 4096.f, 1.f / 8.f);
    frameData_.tonemap_ = tonemapChoice(ctx.settings().tonemap_).operator_;
    frameData_.exposure_ =
        exposure_.update(frame, args.camera_, reg->get<Camera_C>(args.camera_), dt);
    frameData_.frameIndex_ = ++giFrame_;
    frameData_.giWidth_ = static_cast<uint32_t>(giUpdate_.size().x());
    frameData_.giHeight_ = static_cast<uint32_t>(giUpdate_.size().y());
    frameData_.giMaxAge_ = static_cast<uint32_t>(std::lround(3.f / dt));
    frameData_.giDt_ = dt;
    frameData_.giBounces_ = std::clamp(ctx.settings().giBounces_, 1u, 4u);
    frameData_.giPasses_ = std::clamp(ctx.settings().giPasses_, 1u, 8u);

    writeFrameSet(frame, args, ctx);

    PassContext pass{};
    pass.layout_ = pipelineLayout_;
    pass.rtLayout_ = rtPipelineLayout_;
    pass.sets_ = {resources_.textureSet(), frameSets_[frame]};
    if (rayTracing)
        pass.rtSet_ = rtScene_->set(frame);
    pass.push_.cameraIndex_ = camID;
    pass.push_.debugFlags_ =
        (args.showShadowCascades_ ? DebugShadowCascades : 0u) |
        (args.showRtFirstHit_ ? DebugRtFirstHit : 0u) | (args.showGiCells_ ? DebugGiCells : 0u) |
        (args.showGiSides_ ? DebugGiSides : 0u) | (args.showGiCache_ ? DebugGiCache : 0u);
    pass.reg_ = reg;
    pass.instanceManager_ = instanceM;
    pass.assetManager_ = ctx.assetManager_.get();
    pass.rayTracing_ = rayTracing;
    instanceM->forEachPool(
        [&](auto& pool)
        {
            using InstanceT = typename std::remove_reference_t<decltype(pool)>::InstanceType;
            if constexpr (requires { InstanceT::CountField; })
                pass.push_.*InstanceT::CountField = static_cast<uint32_t>(pool.size());
        });

    const GraphBuffer frameConstants = graph.importBuffer("frame constants", resources_,
                                                          frameConstants_, Usage::None, Usage::None);
    graph.addPass("frame constants")
        .write(frameConstants, Usage::TransferDst)
        .execute(
            [frameConstants, data = frameData_](GraphContext& c)
            { vkCmdUpdateBuffer(c.cmd(), c.buffer(frameConstants), 0, sizeof data, &data); });

    if (rayTracing)
    {
        giUpdate_.addPasses(graph, rt, uploads, frameConstants, pass, frame, frameData_.giPasses_);
        const RtScene::CacheStats stats = rtScene_->addCacheStatsPass(graph, rt.cacheStats_, frame);
        if (args.showGiCache_ && giFrame_ % 120 == 0)
            std::cout << "[GI] cache: " << stats.occupied_ << " / " << RtCacheCapacity
                      << " cells, +" << stats.inserted_ << " inserted, " << stats.failed_
                      << " failed, " << stats.evicted_ << " evicted, " << stats.tailMiss_
                      << " tail misses; " << frameData_.giPasses_ << " passes, "
                      << giUpdate_.gpuMs() << " ms\n";
    }

    const auto atlas = [&](const char* name, GPUResourceHandle image, uint32_t size,
                           std::span<const ShadowView> views)
    {
        return ShadowAtlasViews{
            name, graph.importImage(name, resources_, image, Usage::ShaderRead, Usage::ShaderRead),
            size, views};
    };
    const std::array<ShadowAtlasViews, 2> shadowAtlases{{
        atlas("local shadow atlas", localAtlas_, LocalAtlasSize, shadowViews_),
        atlas("cascade shadow atlas", cascadeAtlas_, CascadeAtlasSize, cascadeViews_),
    }};
    const GraphBuffer shadows =
        graph.importBuffer("shadows", resources_, shadow_.buffer(), Usage::None, Usage::ShaderRead);
    shadow_.addPasses(graph, shadows, uploads, pass, shadowAtlases, shadowEntries, terrain_);

    const GraphImage hdr =
        graph.importImage("hdr", resources_, hdrTarget_, Usage::ShaderRead, Usage::ShaderRead);

    const bool rtFirstHit = rayTracing && (pass.push_.debugFlags_ & DebugRtFirstHit) != 0u;
    GraphPass& scene =
        graph.addPass("scene")
            .color(hdr, Load::Clear, targets.clearColor_)
            // Reversed Z (Camera_C::make_proj): the far plane is 0.
            .depth(targets.depth_, Load::Clear, 0.f)
            .read(uploads, Usage::AnyRead)
            .read(frameConstants, Usage::ShaderRead)
            .read(shadowAtlases[0].atlas_, Usage::ShaderRead)
            .read(shadowAtlases[1].atlas_, Usage::ShaderRead)
            .execute(
                [this, pass, rtFirstHit](GraphContext& c)
                {
                    const PassContext context = bindPass(pass, c.cmd());
                    if (rtFirstHit)
                        rtDebug_.record(context);
                    else
                        recordScene(context);
                });
    if (shadowEntries != 0)
        scene.read(shadows, Usage::ShaderRead);
    if (rayTracing)
        scene.read(rt.cacheKeys_, Usage::ShaderRead)
            .read(rt.cacheValues_, Usage::ShaderRead)
            .read(rt.tlas_, Usage::AccelerationStructureRead)
            .read(rt.blases_, Usage::AccelerationStructureRead);

    exposure_.addPasses(graph, hdr, frameConstants, pass, frame);

    graph.addPass("tonemap")
        .color(targets.color_, Load::DontCare)
        .depth(targets.depth_, Load::Keep)
        .read(uploads, Usage::AnyRead)
        .read(hdr, Usage::ShaderRead)
        .read(frameConstants, Usage::ShaderRead)
        .execute(
            [this, pass](GraphContext& c)
            {
                const PassContext context = bindPass(pass, c.cmd());
                tonemap_.record(context);
                // After the sky: it writes no depth, so it would paint over any wire drawn
                // against the background.
                debug_.record(context);
            });
    return true;
}

void ScenePasses::recordScene(const PassContext& pass)
{
    geometry_.record(pass);
    terrain_.record(pass);
    // Before the sky, like the geometry: they write depth, so the sky's
    // LESS_OR_EQUAL test then leaves their pixels alone.
    billboards_.record(pass);
    sky_.record(pass);
}
}  // namespace batap
