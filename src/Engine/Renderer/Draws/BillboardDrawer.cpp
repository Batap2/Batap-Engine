#include "Renderer/Draws/BillboardDrawer.h"

#include "Assets/AssetManager.h"
#include "Renderer/Billboards.h"
#include "Renderer/Device/Formats.h"
#include "Renderer/Device/Pipelines.h"
#include "Renderer/Device/ResourceManager.h"
#include "Renderer/Device/VulkanContext.h"
#include "Renderer/Draws/MeshDrawer.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace batap
{
namespace
{
constexpr uint32_t kMaxBillboards = 65536;
}

BillboardDrawer::BillboardDrawer(const DrawSetup& setup) : setup_(setup)
{
    buffer_ = setup_.resources_.createPerFrameBuffer(sizeof(BillboardGPUData) * kMaxBillboards,
                                                     "billboards");
}

BillboardDrawer::~BillboardDrawer()
{
    vkDestroyPipeline(setup_.ctx_.device_, pipeline_, nullptr);
    vkDestroyPipeline(setup_.ctx_.device_, translucentPipeline_, nullptr);
    setup_.resources_.requestDestroy(buffer_);
}

void BillboardDrawer::buildPipelines(const ShaderModules& modules)
{
    vkDestroyPipeline(setup_.ctx_.device_, pipeline_, nullptr);
    vkDestroyPipeline(setup_.ctx_.device_, translucentPipeline_, nullptr);

    // Not culled: a Y-locked quad is seen from behind as soon as the camera
    // passes it.
    pipeline_ = GraphicsPipelineBuilder()
                    .shaders(modules[BillboardVS], modules[BillboardPS])
                    .colorFormat(setup_.colorFormat_)
                    .depth(setup_.depthFormat_, true, VK_COMPARE_OP_GREATER)
                    .build(setup_.ctx_.device_, setup_.layout_);
    translucentPipeline_ =
        GraphicsPipelineBuilder()
            .shaders(modules[BillboardVS], modules[BillboardTranslucentPS])
            .colorFormat(TranslucentAccumFormat,
                         blendState(VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE))
            .colorFormat(TranslucentRevealFormat,
                         blendState(VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR))
            .depth(setup_.depthFormat_, false, VK_COMPARE_OP_GREATER_OR_EQUAL)
            .build(setup_.ctx_.device_, setup_.layout_);
}

void BillboardDrawer::upload(const Billboards& billboards, const AssetManager& assets)
{
    const auto& records = billboards.records();
    const auto total = static_cast<uint32_t>(std::min<size_t>(records.size(), kMaxBillboards));
    count_ = 0;
    translucent_ = 0;
    if (total == 0)
        return;

    // Translucent ones (material albedo alpha below 1, as for meshes) packed
    // after the cutout ones, for the translucent pass.
    const AssetGPUArena<Material>& materials = *assets.getGPUArena<Material>();
    std::vector<uint32_t> order(total);
    for (uint32_t i = 0; i < total; ++i)
    {
        const Material* material = materials.atSlot(records[i].materialIdx_);
        if (material && isTranslucent(*material))
            order[total - 1 - translucent_++] = i;
        else
            order[count_++] = i;
    }

    std::vector<BillboardGPUData> out(total);
    for (uint32_t i = 0; i < total; ++i)
    {
        const Billboards::Record& r = records[order[i]];
        BillboardGPUData& g = out[i];
        g.pos_[0] = r.pos_.x();
        g.pos_[1] = r.pos_.y();
        g.pos_[2] = r.pos_.z();
        g.rot_[0] = r.rot_.x();
        g.rot_[1] = r.rot_.y();
        g.rot_[2] = r.rot_.z();
        g.rot_[3] = r.rot_.w();
        g.sizeX_ = r.size_.x();
        g.sizeY_ = r.size_.y();
        g.tint_[0] = r.tint_.x();
        g.tint_[1] = r.tint_.y();
        g.tint_[2] = r.tint_.z();
        g.tint_[3] = r.alpha_;
        g.materialIdx_ = r.materialIdx_;
        g.textureIdx_ = r.textureIdx_;
        g.flags_ = r.flags_;
    }

    const uint64_t bytes = sizeof(BillboardGPUData) * out.size();
    std::memcpy(setup_.resources_.requestUpload(buffer_, bytes).data(), out.data(), bytes);
}

void BillboardDrawer::record(const DrawContext& draw)
{
    if (count_ == 0)
        return;

    vkCmdBindPipeline(draw.cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    DrawPush push = draw.push_;
    push.instanceIndex_ = 0;
    pushDraw(draw, push);
    vkCmdDraw(draw.cmd_, 6, count_, 0, 0);
}

void BillboardDrawer::recordTranslucent(const DrawContext& draw)
{
    if (translucent_ == 0)
        return;

    vkCmdBindPipeline(draw.cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, translucentPipeline_);
    DrawPush push = draw.push_;
    push.instanceIndex_ = count_;
    pushDraw(draw, push);
    vkCmdDraw(draw.cmd_, 6, translucent_, 0, 0);
}
}  // namespace batap
