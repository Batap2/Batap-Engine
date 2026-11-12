#include "Renderer/Draws/ScatterDrawer.h"

#include "Assets/AssetManager.h"
#include "Assets/Mesh.h"
#include "Flatten.h"
#include "Renderer/Device/Formats.h"
#include "Renderer/Device/Pipelines.h"
#include "Renderer/Device/ResourceManager.h"
#include "Renderer/Device/VulkanContext.h"
#include "Renderer/EngineConfig.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <span>

namespace batap
{
namespace
{
constexpr uint32_t kMaxBatches = 65536;
constexpr uint32_t kMaxOwners = 64;
constexpr uint64_t kInitialInstances = 4096;
constexpr uint64_t kRetireFrames = FramesInFlight + 1;

// Depth clamp keeps every caster between the cascade and the light, so only
// a batch past the cascade, or beside it as the light sees it, can go.
bool castsInto(const v4f& batch, const v4f& cascade, const v3f& lightDir)
{
    const v3f offset = batch.head<3>() - cascade.head<3>();
    const float along = offset.dot(lightDir);
    const float reach = batch.w() + cascade.w();
    return along <= reach && (offset - along * lightDir).squaredNorm() <= reach * reach;
}

// A receiver inside a finer cascade's sphere reads that one, blending into the
// next only across its outer CascadeFadeBand (Shadows.hlsli): a shadow kept
// inside the shrunk sphere is never read from a coarser cascade.
bool shadowStaysIn(const v4f& batch, float length, const v3f& lightDir,
                   std::span<const v4f> finerCascades)
{
    const v3f near = batch.head<3>();
    const v3f far = near + lightDir * length;
    for (const v4f& finer : finerCascades)
    {
        const float inside = finer.w() * (1.f - CascadeFadeBand) - batch.w();
        if (inside > 0.f && (near - finer.head<3>()).squaredNorm() <= inside * inside &&
            (far - finer.head<3>()).squaredNorm() <= inside * inside)
            return true;
    }
    return false;
}

bool shadowInView(const v4f& batch, float length, const v3f& lightDir, const v3f& forward,
                  float cone)
{
    const v3f centre = batch.head<3>() + lightDir * (0.5f * length);
    const float radius = batch.w() + 0.5f * length;
    const float distance = centre.norm();
    if (distance <= radius)
        return true;
    const float angle = std::acos(std::clamp(centre.dot(forward) / distance, -1.f, 1.f));
    return angle - std::asin(radius / distance) <= cone;
}
}  // namespace

ScatterDrawer::ScatterDrawer(const DrawSetup& setup) : setup_(setup)
{
    batches_ = setup_.resources_.createStaticBuffer(sizeof(ScatterBatchGPUData) * kMaxBatches,
                                                    "scatter batches");
    owners_ = setup_.resources_.createPerFrameBuffer(sizeof(ScatterOwnerGPUData) * kMaxOwners,
                                                     "scatter owners");
    instances_ =
        setup_.resources_.createStaticBuffer(sizeof(ScatterInstanceGPUData) * kInitialInstances);
    capacity_ = kInitialInstances;
}

ScatterDrawer::~ScatterDrawer()
{
    vkDestroyPipeline(setup_.ctx_.device_, prepassPipeline_, nullptr);
    vkDestroyPipeline(setup_.ctx_.device_, reflectPrepassPipeline_, nullptr);
    vkDestroyPipeline(setup_.ctx_.device_, pipeline_, nullptr);
    vkDestroyPipeline(setup_.ctx_.device_, rtPipeline_, nullptr);
    vkDestroyPipeline(setup_.ctx_.device_, shadowPipeline_, nullptr);
    vkDestroyPipeline(setup_.ctx_.device_, impostorPrepassPipeline_, nullptr);
    vkDestroyPipeline(setup_.ctx_.device_, impostorReflectPrepassPipeline_, nullptr);
    vkDestroyPipeline(setup_.ctx_.device_, impostorPipeline_, nullptr);
    vkDestroyPipeline(setup_.ctx_.device_, impostorRtPipeline_, nullptr);
    setup_.resources_.requestDestroy(batches_);
    setup_.resources_.requestDestroy(owners_);
    setup_.resources_.requestDestroy(instances_);
    for (const auto& [frame, buffer] : retiredBuffers_)
        setup_.resources_.requestDestroy(buffer);
}

void ScatterDrawer::buildPipelines(const ShaderModules& modules)
{
    vkDestroyPipeline(setup_.ctx_.device_, prepassPipeline_, nullptr);
    vkDestroyPipeline(setup_.ctx_.device_, reflectPrepassPipeline_, nullptr);
    vkDestroyPipeline(setup_.ctx_.device_, pipeline_, nullptr);
    vkDestroyPipeline(setup_.ctx_.device_, rtPipeline_, nullptr);
    vkDestroyPipeline(setup_.ctx_.device_, shadowPipeline_, nullptr);
    vkDestroyPipeline(setup_.ctx_.device_, impostorPrepassPipeline_, nullptr);
    vkDestroyPipeline(setup_.ctx_.device_, impostorReflectPrepassPipeline_, nullptr);
    vkDestroyPipeline(setup_.ctx_.device_, impostorPipeline_, nullptr);
    vkDestroyPipeline(setup_.ctx_.device_, impostorRtPipeline_, nullptr);

    const auto geometry = [&](VkShaderModule pixelShader)
    {
        return GraphicsPipelineBuilder()
            .shaders(modules[ScatterVS], pixelShader)
            .vertexAttribute(Mesh::Position, VK_FORMAT_R32G32B32_SFLOAT, 12)
            .vertexAttribute(Mesh::Normal, VK_FORMAT_R32G32B32_SFLOAT, 12)
            .vertexAttribute(Mesh::UV0, VK_FORMAT_R32G32_SFLOAT, 8)
            .vertexAttribute(Mesh::Tangent, VK_FORMAT_R32G32B32A32_SFLOAT, 16)
            .cullBack();
    };
    prepassPipeline_ = geometry(modules[PrepassPS])
                           .colorFormat(ViewNormalFormat)
                           .depth(setup_.depthFormat_, true, VK_COMPARE_OP_GREATER)
                           .build(setup_.ctx_.device_, setup_.layout_);
    reflectPrepassPipeline_ = geometry(modules[PrepassScatterRtPS])
                                  .colorFormat(ViewNormalFormat)
                                  .colorFormat(ReflectSurfaceFormat)
                                  .depth(setup_.depthFormat_, true, VK_COMPARE_OP_GREATER)
                                  .build(setup_.ctx_.device_, setup_.layout_);
    const auto lit = [&](VkShaderModule pixelShader, VkPipelineLayout layout)
    {
        return geometry(pixelShader)
            .colorFormat(setup_.colorFormat_)
            .depth(setup_.depthFormat_, false, VK_COMPARE_OP_EQUAL)
            .build(setup_.ctx_.device_, layout);
    };
    pipeline_ = lit(modules[ScatterPS], setup_.layout_);
    rtPipeline_ = modules[ScatterRtPS] ? lit(modules[ScatterRtPS], setup_.rtLayout_)
                                       : VK_NULL_HANDLE;

    shadowPipeline_ = GraphicsPipelineBuilder()
                          .shaders(modules[ScatterShadowVS], VK_NULL_HANDLE)
                          .vertexAttribute(Mesh::Position, VK_FORMAT_R32G32B32_SFLOAT, 12)
                          .depth(VK_FORMAT_D32_SFLOAT, true, VK_COMPARE_OP_LESS)
                          .depthOnly()
                          .depthClamp()
                          .cullFront()
                          .build(setup_.ctx_.device_, setup_.layout_);

    impostorPrepassPipeline_ = GraphicsPipelineBuilder()
                                   .shaders(modules[ImpostorVS], modules[ImpostorPrepassPS])
                                   .colorFormat(ViewNormalFormat)
                                   .depth(setup_.depthFormat_, true, VK_COMPARE_OP_GREATER)
                                   .build(setup_.ctx_.device_, setup_.layout_);
    impostorReflectPrepassPipeline_ =
        GraphicsPipelineBuilder()
            .shaders(modules[ImpostorVS], modules[ImpostorPrepassRtPS])
            .colorFormat(ViewNormalFormat)
            .colorFormat(ReflectSurfaceFormat)
            .depth(setup_.depthFormat_, true, VK_COMPARE_OP_GREATER)
            .build(setup_.ctx_.device_, setup_.layout_);
    const auto impostorLit = [&](VkShaderModule pixelShader, VkPipelineLayout layout)
    {
        return GraphicsPipelineBuilder()
            .shaders(modules[ImpostorVS], pixelShader)
            .colorFormat(setup_.colorFormat_)
            .depth(setup_.depthFormat_, false, VK_COMPARE_OP_EQUAL)
            .build(setup_.ctx_.device_, layout);
    };
    impostorPipeline_ = impostorLit(modules[ImpostorPS], setup_.layout_);
    impostorRtPipeline_ = modules[ImpostorRtPS]
                              ? impostorLit(modules[ImpostorRtPS], setup_.rtLayout_)
                              : VK_NULL_HANDLE;
}

void ScatterDrawer::reserve(uint64_t instances)
{
    if (instances <= capacity_)
        return;
    const uint64_t capacity = std::max(instances, capacity_ * 2);
    // Unnamed: a named handle is the hash of its name, so the grown buffer
    // would come back under the old one's handle.
    const GPUResourceHandle grown =
        setup_.resources_.createStaticBuffer(capacity * sizeof(ScatterInstanceGPUData));
    setup_.resources_.requestCopy(grown, instances_, end_ * sizeof(ScatterInstanceGPUData));
    // Not requestDestroy yet: it drops the uploads still queued for the
    // buffer, which the copy has to read.
    retiredBuffers_.emplace_back(frame_, instances_);
    instances_ = grown;
    capacity_ = capacity;
}

uint64_t ScatterDrawer::allocate(uint64_t count)
{
    for (auto it = free_.begin(); it != free_.end(); ++it)
        if (it->second >= count)
        {
            const uint64_t first = it->first;
            const uint64_t left = it->second - count;
            free_.erase(it);
            if (left > 0)
                free_.emplace(first + count, left);
            return first;
        }
    const uint64_t first = end_;
    end_ += count;
    return first;
}

void ScatterDrawer::release(uint64_t first, uint64_t count)
{
    auto next = free_.lower_bound(first);
    if (next != free_.end() && first + count == next->first)
    {
        count += next->second;
        next = free_.erase(next);
    }
    if (next != free_.begin())
        if (auto prev = std::prev(next); prev->first + prev->second == first)
        {
            first = prev->first;
            count += prev->second;
            free_.erase(prev);
        }
    if (first + count == end_)
        end_ = first;
    else
        free_.emplace(first, count);
}

void ScatterDrawer::upload(ScatterDraws& draws)
{
    ++frame_;
    for (const uint64_t id : draws.removed_)
        if (const auto it = slots_.find(id); it != slots_.end())
        {
            retiredSlots_.emplace_back(frame_, it->second);
            slots_.erase(it);
        }
    draws.removed_.clear();

    std::erase_if(retiredSlots_,
                  [&](const auto& retired)
                  {
                      if (retired.first + kRetireFrames > frame_)
                          return false;
                      if (retired.second.count_ > 0)
                          release(retired.second.first_, retired.second.count_);
                      if (retired.second.batch_ != InvalidGPUIndex)
                          freeBatches_.push_back(retired.second.batch_);
                      return true;
                  });
    std::erase_if(retiredBuffers_,
                  [&](const auto& retired)
                  {
                      if (retired.first + kRetireFrames > frame_)
                          return false;
                      setup_.resources_.requestDestroy(retired.second);
                      return true;
                  });

    uint64_t need = end_;
    for (const auto& [id, batch] : draws.added_)
        need += batch.instances_.size();
    reserve(need);

    for (auto& [id, batch] : draws.added_)
    {
        Slot slot;
        slot.mesh_ = batch.mesh_;
        slot.shadowMesh_ = batch.shadowMesh_ ? batch.shadowMesh_ : batch.mesh_;
        for (size_t i = 0; i < slot.materials_.size(); ++i)
            slot.materials_[i] = batch.materials_[i] ? batch.materials_[i].index : InvalidGPUIndex;
        slot.castShadows_ = batch.castShadows_;
        slot.shadowLength_ = batch.shadowLength_;
        slot.impostorRadius_ = batch.impostorRadius_;
        slot.fadeStart_ = batch.fadeStart_;
        slot.fadeEnd_ = batch.fadeEnd_;
        slot.count_ = batch.instances_.size();
        if (!freeBatches_.empty())
        {
            slot.batch_ = freeBatches_.back();
            freeBatches_.pop_back();
        }
        else if (nextBatch_ < kMaxBatches)
        {
            slot.batch_ = nextBatch_++;
        }
        else if (!warnedFull_)
        {
            std::cerr << "[Scatter] more than " << kMaxBatches << " batches, the rest not drawn\n";
            warnedFull_ = true;
        }
        if (slot.count_ > 0)
        {
            slot.first_ = allocate(slot.count_);
            const std::span<std::byte> bytes = setup_.resources_.requestUpload(
                instances_, slot.count_ * sizeof(ScatterInstanceGPUData),
                slot.first_ * sizeof(ScatterInstanceGPUData));
            std::vector<ScatterInstanceGPUData> out(batch.instances_.size());
            for (size_t i = 0; i < out.size(); ++i)
            {
                const ScatterDraws::Instance& in = batch.instances_[i];
                flatten(out[i].position_, in.position_);
                out[i].scale_ = in.scale_;
                flatten(out[i].rotation_, in.rotation_.coeffs());
            }
            std::memcpy(bytes.data(), out.data(), bytes.size());
        }
        slots_[id] = slot;
    }
    draws.added_.clear();

    const auto cells = [](const v3d& p, int32_t (&cell)[3], float (&offset)[3])
    {
        const v3d whole = (p / double(GiCellBase)).array().floor();
        const v3i c = whole.cast<int>();
        std::memcpy(cell, c.data(), sizeof cell);
        flatten(offset, (p - whole * double(GiCellBase)).cast<float>());
    };
    const auto matrix = [](const m3f& m)
    {
        m4f out = m4f::Identity();
        out.topLeftCorner<3, 3>() = m;
        return out;
    };
    ownerData_.clear();
    for (const ScatterDraws::Owner& owner : draws.owners_)
    {
        auto [it, fresh] = ownerSlots_.try_emplace(owner.owner_);
        OwnerSlot& slot = it->second;
        if (fresh)
            slot.index_ = static_cast<uint32_t>(ownerSlots_.size() - 1);
        if (slot.index_ >= kMaxOwners)
            continue;
        const ScatterDraws::Owner& before = slot.frame_ + 1 == frame_ ? slot.last_ : owner;
        if (ownerData_.size() <= slot.index_)
            ownerData_.resize(slot.index_ + 1);
        ScatterOwnerGPUData& gpu = ownerData_[slot.index_];
        flatten(gpu.linear_, matrix(owner.linear_));
        flatten(gpu.axes_, matrix(owner.axes_));
        flatten(gpu.prevLinear_, matrix(before.linear_));
        flatten(gpu.prevAxes_, matrix(before.axes_));
        cells(owner.camera_, gpu.cameraCell_, gpu.cameraOffset_);
        cells(before.camera_, gpu.prevCameraCell_, gpu.prevCameraOffset_);
        slot.last_ = owner;
        slot.frame_ = frame_;
    }
    if (!ownerData_.empty())
    {
        const uint64_t bytes = sizeof(ScatterOwnerGPUData) * ownerData_.size();
        std::memcpy(setup_.resources_.requestUpload(owners_, bytes).data(), ownerData_.data(),
                    bytes);
    }

    draws_.clear();
    for (const auto& [id, placement] : draws.placed_)
    {
        const auto it = slots_.find(id);
        if (it == slots_.end() || it->second.count_ == 0 || it->second.batch_ == InvalidGPUIndex)
            continue;
        const auto owner = ownerSlots_.find(placement.owner_);
        if (owner == ownerSlots_.end() || owner->second.index_ >= kMaxOwners)
            continue;
        Slot& slot = it->second;
        if (!slot.written_)
        {
            writeBatch(slot, placement, owner->second.index_);
            slot.written_ = true;
        }
        const v3f& centre = placement.offset_;
        draws_.push_back({slot.mesh_, slot.shadowMesh_,
                          v4f{centre.x(), centre.y(), centre.z(), placement.radius_},
                          slot.shadowLength_, slot.batch_, static_cast<uint32_t>(slot.count_),
                          slot.castShadows_, placement.visible_, slot.impostorRadius_ > 0.f});
    }
}

// The centre in the owner's frame is taken at the batch's first placement and
// kept: an owner whose scale changes afterwards would misplace it.
void ScatterDrawer::writeBatch(const Slot& slot, const ScatterDraws::Placement& placement,
                               uint32_t owner)
{
    ScatterBatchGPUData gpu{};
    const v3d whole = (placement.centreInOwner_ / double(GiCellBase)).array().floor();
    const v3i cell = whole.cast<int>();
    std::memcpy(gpu.centreCell_, cell.data(), sizeof gpu.centreCell_);
    flatten(gpu.centreOffset_,
            (placement.centreInOwner_ - whole * double(GiCellBase)).cast<float>());
    gpu.ownerSlot_ = owner;
    gpu.owner_ = placement.owner_;
    std::copy(slot.materials_.begin(), slot.materials_.end(),
              std::span{gpu.materialIndices_}.begin());
    gpu.firstInstance_ = static_cast<uint32_t>(slot.first_);
    gpu.fadeStart_ = slot.fadeStart_;
    gpu.fadeEnd_ = slot.fadeEnd_;
    gpu.impostorRadius_ = slot.impostorRadius_;
    std::memcpy(
        setup_.resources_.requestUpload(batches_, sizeof gpu, uint64_t{slot.batch_} * sizeof gpu)
            .data(),
        &gpu, sizeof gpu);
}

bool ScatterDrawer::castsShadow(const Draw& d, const ScatterShadowView& view)
{
    if (!d.castShadows_)
        return false;
    if (view.cascade_.w() <= 0.f)
        return true;
    if (!castsInto(d.bounds_, view.cascade_, view.lightDir_))
        return false;
    if (d.shadowLength_ <= 0.f)
        return true;
    if (view.viewCone_ > 0.f && !shadowInView(d.bounds_, d.shadowLength_, view.lightDir_,
                                              view.viewForward_, view.viewCone_))
        return false;
    return !shadowStaysIn(d.bounds_, d.shadowLength_, view.lightDir_, view.finerCascades_);
}

void ScatterDrawer::recordDraws(const DrawContext& draw, VkPipeline pipeline, uint32_t streams,
                                const ScatterShadowView* shadow) const
{
    if (draws_.empty() || pipeline == VK_NULL_HANDLE)
        return;
    vkCmdBindPipeline(draw.cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    DrawPush push = draw.push_;
    for (const Draw& d : draws_)
    {
        if (shadow ? !castsShadow(d, *shadow) : !d.visible_)
            continue;
        const MeshHandle drawn = shadow ? d.shadowMesh_ : d.mesh_;
        const Mesh* mesh = drawn ? draw.assetManager_->get(drawn) : nullptr;
        if (!mesh || !mesh->isRenderable())
            continue;

        const VkBuffer meshBuffer = setup_.resources_.bufferFor(mesh->buffer_);
        const std::array<VkBuffer, Mesh::VertexStreams> vertexBuffers{meshBuffer, meshBuffer,
                                                                      meshBuffer, meshBuffer};
        vkCmdBindVertexBuffers(draw.cmd_, 0, streams, vertexBuffers.data(),
                               mesh->streamOffsets_.data());
        vkCmdBindIndexBuffer(draw.cmd_, meshBuffer, mesh->streamOffsets_[Mesh::Index],
                             toVkIndexType(mesh->indexFormat_));

        push.instanceIndex_ = d.batch_;
        if (mesh->subMeshCount == 0)
        {
            push.submeshIndex_ = 0;
            pushDraw(draw, push);
            vkCmdDrawIndexed(draw.cmd_, mesh->indexCount_, d.count_, 0, 0, 0);
            continue;
        }
        for (uint8_t sub = 0; sub < mesh->subMeshCount; ++sub)
        {
            push.submeshIndex_ = sub;
            pushDraw(draw, push);
            const auto& subMesh = mesh->subMeshes[sub];
            vkCmdDrawIndexed(draw.cmd_, subMesh.indexCount, d.count_, subMesh.indexOffset, 0, 0);
        }
    }
}

void ScatterDrawer::recordPrepass(const DrawContext& draw)
{
    recordDraws(draw, draw.reflections_ ? reflectPrepassPipeline_ : prepassPipeline_,
                Mesh::VertexStreams, nullptr);
    recordImpostors(draw,
                    draw.reflections_ ? impostorReflectPrepassPipeline_ : impostorPrepassPipeline_);
}

void ScatterDrawer::record(const DrawContext& draw)
{
    recordDraws(draw, draw.rayTracing_ ? rtPipeline_ : pipeline_, Mesh::VertexStreams, nullptr);
    recordImpostors(draw, draw.rayTracing_ ? impostorRtPipeline_ : impostorPipeline_);
}

void ScatterDrawer::recordImpostors(const DrawContext& draw, VkPipeline pipeline) const
{
    if (pipeline == VK_NULL_HANDLE)
        return;
    bool bound = false;
    DrawPush push = draw.push_;
    push.submeshIndex_ = 0;
    for (const Draw& d : draws_)
    {
        if (!d.impostor_ || !d.visible_)
            continue;
        if (!bound)
        {
            vkCmdBindPipeline(draw.cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            bound = true;
        }
        push.instanceIndex_ = d.batch_;
        pushDraw(draw, push);
        vkCmdDraw(draw.cmd_, 6, d.count_, 0, 0);
    }
}

void ScatterDrawer::recordShadow(const DrawContext& draw, const ScatterShadowView& view)
{
    recordDraws(draw, shadowPipeline_, 1, &view);
}

bool ScatterDrawer::hasShadowCasters() const
{
    return std::ranges::any_of(draws_, [](const Draw& d) { return d.castShadows_; });
}

}  // namespace batap
