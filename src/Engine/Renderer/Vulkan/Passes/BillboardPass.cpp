#include "Renderer/Vulkan/Passes/BillboardPass.h"

#include "Renderer/Billboards.h"
#include "Renderer/Vulkan/VulkanContext.h"
#include "Renderer/Vulkan/VulkanPipelines.h"
#include "Renderer/Vulkan/VulkanResources.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace batap
{
namespace
{
constexpr uint32_t kMaxBillboards = 65536;
}

BillboardPass::BillboardPass(const PassSetup& setup) : setup_(setup)
{
    buffer_ = setup_.resources_.createPerFrameBuffer(sizeof(BillboardGPUData) * kMaxBillboards,
                                                     "billboards");
}

BillboardPass::~BillboardPass()
{
    vkDestroyPipeline(setup_.ctx_.device_, pipeline_, nullptr);
    setup_.resources_.requestDestroy(buffer_);
}

void BillboardPass::buildPipelines(const ShaderModules& modules)
{
    if (pipeline_)
        vkDestroyPipeline(setup_.ctx_.device_, pipeline_, nullptr);

    // Not culled: a Y-locked quad is seen from behind as soon as the camera
    // passes it.
    pipeline_ = GraphicsPipelineBuilder()
                    .shaders(modules[BillboardVS], modules[BillboardPS])
                    .colorFormat(setup_.colorFormat_)
                    .depth(setup_.depthFormat_, true, VK_COMPARE_OP_LESS)
                    .build(setup_.ctx_.device_, setup_.layout_);
}

void BillboardPass::upload(const Billboards& billboards)
{
    const auto& records = billboards.records();
    count_ = static_cast<uint32_t>(std::min<size_t>(records.size(), kMaxBillboards));
    if (count_ == 0)
        return;

    std::vector<BillboardGPUData> out(count_);
    for (uint32_t i = 0; i < count_; ++i)
    {
        const Billboards::Record& r = records[i];
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

void BillboardPass::record(const PassContext& pass)
{
    if (count_ == 0)
        return;

    vkCmdBindPipeline(pass.cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    DrawPush push = pass.push_;
    push.instanceIndex_ = 0;
    pushDraw(pass, push);
    vkCmdDraw(pass.cmd_, 6, count_, 0, 0);
}
}  // namespace batap
