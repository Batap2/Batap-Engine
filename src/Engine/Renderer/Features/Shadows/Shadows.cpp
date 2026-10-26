#include "Renderer/Features/Shadows/Shadows.h"

#include "Assets/Mesh.h"
#include "Components/Camera_C.h"
#include "Components/DirectionalLight_C.h"
#include "Components/FarPointLight_C.h"
#include "Components/Name_C.h"
#include "Components/Transform_C.h"
#include "Flatten.h"
#include "Instance/InstanceManager.h"
#include "Renderer/Device/Pipelines.h"
#include "Renderer/Device/ResourceManager.h"
#include "Renderer/Device/VulkanContext.h"
#include "Renderer/Draws/MeshDrawer.h"
#include "Renderer/Draws/TerrainDrawer.h"
#include "Renderer/EngineConfig.h"
#include "Renderer/Features/Shadows/ShadowCascades.h"
#include "Renderer/ResourceFormat.h"
#include "Systems/CascadeLight_S.h"

#include <algorithm>
#include <bit>
#include <iostream>
#include <string>

namespace batap
{
namespace
{
constexpr size_t kInitialEntries = 256;

// vkCmdUpdateBuffer's cap.
constexpr size_t kEntriesPerWrite = 65536 / sizeof(ShadowGPUData);
}  // namespace

Shadows::Shadows(const DrawSetup& setup) : setup_(setup)
{
    reserve(kInitialEntries);
    localAtlas_ = setup_.resources_.createTarget(LocalAtlasSize, LocalAtlasSize,
                                                 ResourceFormat::D32_FLOAT, "local shadow atlas");
    cascadeAtlas_ = setup_.resources_.createTarget(
        CascadeAtlasSize, CascadeAtlasSize, ResourceFormat::D32_FLOAT, "cascade shadow atlas");
}

Shadows::~Shadows()
{
    vkDestroyPipeline(setup_.ctx_.device_, pipeline_, nullptr);
}

void Shadows::reserve(size_t entryCount)
{
    if (entryCount <= capacity_)
        return;

    capacity_ = std::max(kInitialEntries, std::bit_ceil(entryCount));

    if (buffer_.valid())
        setup_.resources_.requestDestroy(buffer_);
    buffer_ = setup_.resources_.createPerFrameBuffer(sizeof(ShadowGPUData) * capacity_, "shadows");
}

void Shadows::buildPipelines(const ShaderModules& modules)
{
    if (pipeline_)
        vkDestroyPipeline(setup_.ctx_.device_, pipeline_, nullptr);

    pipeline_ = GraphicsPipelineBuilder()
                    .shaders(modules[ShadowVS], VK_NULL_HANDLE)
                    .vertexAttribute(Mesh::Position, VK_FORMAT_R32G32B32_SFLOAT, 12)
                    .depth(VK_FORMAT_D32_SFLOAT, true, VK_COMPARE_OP_LESS)
                    .depthOnly()
                    .depthClamp()
                    .cullFront()
                    .build(setup_.ctx_.device_, setup_.layout_);
}

void Shadows::prepare(entt::registry& reg, GPUInstanceManager& instances, entt::entity camera,
                      v2i frameSize, bool rayTracing)
{
    localAllocator_.allocate(reg, instances, camera, frameSize, rayTracing, localViews_);
    if (rayTracing)
        cascadeViews_.clear();
    else
        fitCascades(reg, instances, camera, frameSize);
    entryCount_ = instances.pool<LightInstance>().size() * MaxShadowViewsPerLight;
    reserve(entryCount_);
}

void Shadows::fitCascades(entt::registry& reg, GPUInstanceManager& instances, entt::entity camera,
                          v2i frameSize)
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

Shadows::Output Shadows::addPasses(RenderGraph& graph, GraphBuffer uploads, const DrawContext& draw,
                                   TerrainDrawer& terrain)
{
    const auto atlas = [&](const char* name, GPUResourceHandle image, uint32_t size,
                           std::span<const ShadowView> views)
    {
        return AtlasViews{
            name,
            graph.importImage(name, setup_.resources_, image, Usage::ShaderRead, Usage::ShaderRead),
            size, views};
    };
    const std::array<AtlasViews, 2> atlases{{
        atlas("local shadow atlas", localAtlas_, LocalAtlasSize, localViews_),
        atlas("cascade shadow atlas", cascadeAtlas_, CascadeAtlasSize, cascadeViews_),
    }};
    const GraphBuffer shadows =
        graph.importBuffer("shadows", setup_.resources_, buffer_, Usage::None, Usage::ShaderRead);
    const Output output{{atlases[0].atlas_, atlases[1].atlas_}, shadows, entryCount_ != 0};
    if (entryCount_ == 0 || entryCount_ > capacity_)
        return output;

    entries_.assign(entryCount_, ShadowGPUData{});
    for (const AtlasViews& atlasViews : atlases)
    {
        const uint32_t atlasTexture = graph.textureIndex(atlasViews.atlas_);
        const float atlasSize = static_cast<float>(atlasViews.size_);
        for (const ShadowView& view : atlasViews.views_)
        {
            if (view.entry_ >= entryCount_)
                continue;
            ShadowGPUData& entry = entries_[view.entry_];
            flatten(entry.viewProj_, view.viewProj_);
            flatten(entry.sphere_, view.sphere_);
            entry.strength_ = view.strength_;
            entry.texelWorld_ = view.texelWorld_;
            entry.atlasTexture_ = atlasTexture;
            entry.uvScale_ = static_cast<float>(view.size_) / atlasSize;
            entry.uvOffset_[0] = static_cast<float>(view.x_) / atlasSize;
            entry.uvOffset_[1] = static_cast<float>(view.y_) / atlasSize;
            entry.texelUV_ = 1.f / atlasSize;
        }
    }
    const std::span<const ShadowGPUData> all{entries_};
    graph.addPass("shadow entries")
        .write(shadows, Usage::TransferDst)
        .execute(
            [all, shadows](GraphContext& c)
            {
                for (size_t first = 0; first < all.size(); first += kEntriesPerWrite)
                {
                    const auto chunk =
                        all.subspan(first, std::min(kEntriesPerWrite, all.size() - first));
                    vkCmdUpdateBuffer(c.cmd(), c.buffer(shadows), first * sizeof(ShadowGPUData),
                                      chunk.size_bytes(), chunk.data());
                }
            });

    for (const AtlasViews& atlasViews : atlases)
    {
        if (atlasViews.views_.empty())
            continue;
        graph.addPass(atlasViews.name_)
            .depth(atlasViews.atlas_, Load::Clear, 1.f)
            .read(uploads, Usage::AnyRead)
            .read(shadows, Usage::ShaderRead)
            .execute(
                [draw, views = atlasViews.views_, pipeline = pipeline_,
                 &resources = setup_.resources_, &terrain](GraphContext& c)
                {
                    DrawContext shadowDraw = bindDraw(draw, c.cmd());
                    for (const ShadowView& view : views)
                    {
                        shadowDraw.push_.shadowViewIndex_ = view.entry_;
                        setViewportYUpRect(c.cmd(), view.x_, view.y_, view.size_, view.size_);
                        // Again for every view: the terrain binds its own.
                        vkCmdBindPipeline(c.cmd(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                        recordMeshDraws(shadowDraw, resources, 1, true);
                        terrain.recordShadow(shadowDraw);
                    }
                });
    }
    return output;
}
}  // namespace batap
