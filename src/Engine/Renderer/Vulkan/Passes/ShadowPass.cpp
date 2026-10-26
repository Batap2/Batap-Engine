#include "Renderer/Vulkan/Passes/ShadowPass.h"

#include "Assets/Mesh.h"
#include "Flatten.h"
#include "Renderer/EngineConfig.h"
#include "Renderer/Vulkan/Passes/MeshDraws.h"
#include "Renderer/Vulkan/Passes/TerrainPass.h"
#include "Renderer/Vulkan/VulkanContext.h"
#include "Renderer/Vulkan/VulkanPipelines.h"
#include "Renderer/Vulkan/VulkanResources.h"

#include <algorithm>

namespace batap
{
namespace
{
constexpr size_t kInitialEntries = 256;

// vkCmdUpdateBuffer's cap.
constexpr size_t kEntriesPerWrite = 65536 / sizeof(ShadowGPUData);

}  // namespace

ShadowPass::ShadowPass(const PassSetup& setup) : setup_(setup)
{
    reserve(kInitialEntries);
}

void ShadowPass::reserve(size_t entryCount)
{
    if (entryCount <= capacity_)
        return;

    capacity_ = std::max(kInitialEntries, std::bit_ceil(entryCount));

    if (buffer_.valid())
        setup_.resources_.requestDestroy(buffer_);
    buffer_ = setup_.resources_.createPerFrameBuffer(sizeof(ShadowGPUData) * capacity_, "shadows");
}

ShadowPass::~ShadowPass()
{
    vkDestroyPipeline(setup_.ctx_.device_, pipeline_, nullptr);
}

void ShadowPass::buildPipelines(const ShaderModules& modules)
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

void ShadowPass::addPasses(RenderGraph& graph, GraphBuffer shadows, GraphBuffer uploads,
                           const PassContext& pass, std::span<const ShadowAtlasViews> atlases,
                           size_t entryCount, TerrainPass& terrain)
{
    if (entryCount == 0 || entryCount > capacity_)
        return;

    entries_.assign(entryCount, ShadowGPUData{});
    for (const ShadowAtlasViews& atlas : atlases)
    {
        const uint32_t atlasTexture = graph.textureIndex(atlas.atlas_);
        const float atlasSize = static_cast<float>(atlas.size_);
        for (const ShadowView& view : atlas.views_)
        {
            if (view.entry_ >= entryCount)
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

    for (const ShadowAtlasViews& atlas : atlases)
    {
        if (atlas.views_.empty())
            continue;
        // An untouched texel lights whatever samples it.
        graph.addPass(atlas.name_)
            .depth(atlas.atlas_, Load::Clear, 1.f)
            .read(uploads, Usage::AnyRead)
            .read(shadows, Usage::ShaderRead)
            .execute(
                [pass, views = atlas.views_, pipeline = pipeline_,
                 &resources = setup_.resources_, &terrain](GraphContext& c)
                {
                    PassContext shadowPass = bindPass(pass, c.cmd());
                    for (const ShadowView& view : views)
                    {
                        shadowPass.push_.shadowViewIndex_ = view.entry_;
                        setViewportYUpRect(c.cmd(), view.x_, view.y_, view.size_, view.size_);
                        // Again for every view: the terrain binds its own.
                        vkCmdBindPipeline(c.cmd(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                        recordMeshDraws(shadowPass, resources, 1, true);
                        terrain.recordShadow(shadowPass);
                    }
                });
    }
}
}  // namespace batap
