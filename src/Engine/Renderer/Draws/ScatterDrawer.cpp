#include "Renderer/Draws/ScatterDrawer.h"

#include "Assets/AssetManager.h"
#include "Assets/Mesh.h"
#include "Flatten.h"
#include "Renderer/Device/Formats.h"
#include "Renderer/Device/Pipelines.h"
#include "Renderer/Device/ResourceManager.h"
#include "Renderer/Device/VulkanContext.h"
#include "Platform/PlatformWindow.h"
#include "Renderer/EngineConfig.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <span>
#include <string>

namespace batap
{
namespace
{
constexpr uint32_t kMaxBatches = 65536;
constexpr uint32_t kMaxOwners = 64;
constexpr uint64_t kInitialInstances = 4096;
constexpr uint64_t kCommandBytes = sizeof(ScatterDrawCommandGPUData);
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
    instanceBatches_ = setup_.resources_.createStaticBuffer(sizeof(uint32_t) * kInitialInstances);
    groupBuffer_ = setup_.resources_.createPerFrameBuffer(
        sizeof(ScatterDrawGroupGPUData) * ScatterMaxDrawGroups, "scatter draw groups");
    commands_ = setup_.resources_.createPerFrameBuffer(
        kCommandBytes * ScatterCommandViews * ScatterMaxDrawGroups, "scatter draw commands");
    cullData_ = setup_.resources_.createPerFrameBuffer(sizeof(ScatterCullGPUData), "scatter cull");
    visibleCapacity_ = kInitialInstances * ScatterCullViews;
    visible_ = setup_.resources_.createPerFrameBuffer(sizeof(uint32_t) * visibleCapacity_);
    visibility_ = setup_.resources_.createStaticBuffer(sizeof(uint32_t) * kInitialInstances);
    hzbCapacity_ = kInitialInstances;
    hzb_ = setup_.resources_.createPerFrameBuffer(sizeof(float) * hzbCapacity_);
    hiz_ = platformEnvVar("BATAP_HIZ").value_or("1") != "0";
    quadIndices_ = setup_.resources_.createStaticBuffer(6 * sizeof(uint32_t), "impostor quad");
    const std::array<uint32_t, 6> quad{0, 1, 2, 3, 4, 5};
    std::memcpy(setup_.resources_.requestUpload(quadIndices_, sizeof quad).data(), quad.data(),
                sizeof quad);
    groups_.push_back({});
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
    vkDestroyPipeline(setup_.ctx_.device_, cullPipeline_, nullptr);
    vkDestroyPipeline(setup_.ctx_.device_, lateCullPipeline_, nullptr);
    vkDestroyPipeline(setup_.ctx_.device_, hzbPipeline_, nullptr);
    setup_.resources_.requestDestroy(batches_);
    setup_.resources_.requestDestroy(visibility_);
    setup_.resources_.requestDestroy(hzb_);
    setup_.resources_.requestDestroy(groupBuffer_);
    setup_.resources_.requestDestroy(commands_);
    setup_.resources_.requestDestroy(visible_);
    setup_.resources_.requestDestroy(quadIndices_);
    setup_.resources_.requestDestroy(cullData_);
    setup_.resources_.requestDestroy(owners_);
    setup_.resources_.requestDestroy(instances_);
    setup_.resources_.requestDestroy(instanceBatches_);
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
    vkDestroyPipeline(setup_.ctx_.device_, cullPipeline_, nullptr);
    cullPipeline_ =
        buildComputePipeline(setup_.ctx_.device_, setup_.layout_, modules[ScatterCullCS]);
    vkDestroyPipeline(setup_.ctx_.device_, lateCullPipeline_, nullptr);
    lateCullPipeline_ =
        buildComputePipeline(setup_.ctx_.device_, setup_.layout_, modules[ScatterLateCS]);
    vkDestroyPipeline(setup_.ctx_.device_, hzbPipeline_, nullptr);
    hzbPipeline_ =
        buildComputePipeline(setup_.ctx_.device_, setup_.layout_, modules[ScatterHzbCS]);
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
    const GPUResourceHandle grownBatches =
        setup_.resources_.createStaticBuffer(capacity * sizeof(uint32_t));
    setup_.resources_.requestCopy(grownBatches, instanceBatches_, end_ * sizeof(uint32_t));
    retiredBuffers_.emplace_back(frame_, instanceBatches_);
    instanceBatches_ = grownBatches;
    retiredBuffers_.emplace_back(frame_, visibility_);
    visibility_ = setup_.resources_.createStaticBuffer(capacity * sizeof(uint32_t));
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

void ScatterDrawer::upload(ScatterDraws& draws, AssetManager& assets)
{
    ++frame_;
    for (const uint64_t id : draws.removed_)
        if (const auto it = slots_.find(id); it != slots_.end())
        {
            if (it->second.written_)
                countGroups(it->second, -1);
            if (it->second.batch_ != InvalidGPUIndex)
                writeEmpty(it->second.batch_);
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
        slot.radius_ = batch.radius_;
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
        if (slot.batch_ != InvalidGPUIndex)
            writeEmpty(slot.batch_);
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
            const std::span<std::byte> batchBytes = setup_.resources_.requestUpload(
                instanceBatches_, slot.count_ * sizeof(uint32_t), slot.first_ * sizeof(uint32_t));
            const std::vector<uint32_t> batchOfInstance(slot.count_, slot.batch_);
            std::memcpy(batchBytes.data(), batchOfInstance.data(), batchBytes.size());
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
    if (gpuCulling_)
    {
        pending_.insert(pending_.end(), draws.placed_.begin(), draws.placed_.end());
        draws.placed_.clear();
        std::erase_if(pending_,
                      [&](const std::pair<uint64_t, ScatterDraws::Placement>& entry)
                      {
                          const auto it = slots_.find(entry.first);
                          if (it == slots_.end() || it->second.count_ == 0 ||
                              it->second.batch_ == InvalidGPUIndex || it->second.written_)
                              return true;
                          const auto owner = ownerSlots_.find(entry.second.owner_);
                          if (owner == ownerSlots_.end())
                              return false;
                          if (owner->second.index_ >= kMaxOwners)
                              return true;
                          it->second.written_ =
                              writeBatch(it->second, entry.second, owner->second.index_, assets);
                          return it->second.written_;
                      });
        flushBatchWrites();
        reserveVisible();
        return;
    }
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
            slot.written_ = writeBatch(slot, placement, owner->second.index_, assets);
        const v3f& centre = placement.offset_;
        draws_.push_back({slot.mesh_, slot.shadowMesh_,
                          v4f{centre.x(), centre.y(), centre.z(), placement.radius_},
                          slot.shadowLength_, static_cast<uint32_t>(slot.first_),
                          static_cast<uint32_t>(slot.count_),
                          slot.castShadows_, placement.visible_, slot.impostorRadius_ > 0.f});
    }
    draws.placed_.clear();
    flushBatchWrites();
}

// The centre in the owner's frame is taken at the batch's first placement and
// kept: an owner whose scale changes afterwards would misplace it.
bool ScatterDrawer::writeBatch(Slot& slot, const ScatterDraws::Placement& placement,
                               uint32_t owner, AssetManager& assets)
{
    if (slot.impostorRadius_ > 0.f)
    {
        slot.drawGroup_ = ScatterImpostorGroup;
        slot.drawGroups_ = 1;
    }
    else
    {
        std::tie(slot.drawGroup_, slot.drawGroups_) = groupsOf(slot.mesh_, assets);
        if (slot.drawGroups_ == 0)
            return false;
    }
    slot.shadowGroups_ = 0;
    if (slot.castShadows_ && slot.impostorRadius_ <= 0.f && slot.shadowMesh_)
    {
        std::tie(slot.shadowGroup_, slot.shadowGroups_) = groupsOf(slot.shadowMesh_, assets);
        if (slot.shadowGroups_ == 0)
            return false;
    }
    countGroups(slot, 1);

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
    gpu.instanceCount_ = static_cast<uint32_t>(slot.count_);
    gpu.radius_ = slot.radius_;
    gpu.drawGroup_ = slot.drawGroup_;
    gpu.drawGroups_ = slot.drawGroups_;
    gpu.shadowGroup_ = slot.shadowGroup_;
    gpu.shadowGroups_ = slot.shadowGroups_;
    gpu.shadowLength_ = slot.shadowLength_;
    gpu.pieceRadius_ = slot.radius_;
    if (slot.impostorRadius_ > 0.f)
    {
        gpu.pieceRadius_ = 1.5f * slot.impostorRadius_;
    }
    else if (const Mesh* mesh = assets.get(slot.mesh_); mesh && mesh->localBounds_.valid())
    {
        const v3f reach =
            mesh->localBounds_.min_.cwiseAbs().cwiseMax(mesh->localBounds_.max_.cwiseAbs());
        gpu.pieceRadius_ = reach.norm();
    }
    batchWrites_[slot.batch_] = gpu;
    return true;
}

void ScatterDrawer::writeEmpty(uint32_t batch)
{
    batchWrites_[batch] = ScatterBatchGPUData{};
}

void ScatterDrawer::flushBatchWrites()
{
    for (const auto& [batch, gpu] : batchWrites_)
        std::memcpy(
            setup_.resources_.requestUpload(batches_, sizeof gpu, uint64_t{batch} * sizeof gpu)
                .data(),
            &gpu, sizeof gpu);
    batchWrites_.clear();
}

std::pair<uint32_t, uint32_t> ScatterDrawer::groupsOf(MeshHandle handle, AssetManager& assets)
{
    const Mesh* mesh = handle ? assets.get(handle) : nullptr;
    if (!mesh || !mesh->isRenderable())
        return {InvalidGPUIndex, 0};
    const auto count = std::max<uint32_t>(mesh->subMeshCount, 1u);
    const uint64_t key = (uint64_t{handle.index} << 32) | handle.generation;
    const auto [it, fresh] = groupOf_.try_emplace(key, static_cast<uint32_t>(groups_.size()));
    if (fresh)
    {
        if (groups_.size() + count > ScatterMaxDrawGroups)
        {
            groupOf_.erase(it);
            if (!warnedGroups_)
                std::cerr << "[Scatter] more than " << ScatterMaxDrawGroups
                          << " draw groups, the rest not drawn\n";
            warnedGroups_ = true;
            return {InvalidGPUIndex, 0};
        }
        for (uint32_t s = 0; s < count; ++s)
            groups_.push_back({handle, s});
    }
    return {it->second, count};
}

void ScatterDrawer::countGroups(const Slot& slot, int32_t delta)
{
    const auto add = [&](uint64_t& instances)
    {
        if (delta > 0)
            instances += slot.count_;
        else
            instances -= slot.count_;
    };
    for (uint32_t s = 0; s < slot.drawGroups_; ++s)
        add(groups_[slot.drawGroup_ + s].instances_);
    for (uint32_t s = 0; s < slot.shadowGroups_; ++s)
        add(groups_[slot.shadowGroup_ + s].shadowInstances_);
}

void ScatterDrawer::reserveVisible()
{
    uint64_t main = 0;
    uint64_t shadow = 0;
    for (const Group& g : groups_)
    {
        main += g.instances_;
        shadow += g.shadowInstances_;
    }
    const uint64_t need = 2 * main + uint64_t{ScatterCullViews - 1} * shadow;
    if (need <= visibleCapacity_)
        return;
    setup_.resources_.requestDestroy(visible_);
    visibleCapacity_ = std::max(need + need / 4, visibleCapacity_ * 2);
    // Unnamed: a named handle is the hash of its name, so the grown buffer
    // would come back under the old one's handle.
    visible_ = setup_.resources_.createPerFrameBuffer(sizeof(uint32_t) * visibleCapacity_);
}

std::optional<ScatterCullBuffers> ScatterDrawer::addCullPass(RenderGraph& graph,
                                                            const DrawContext& draw,
                                                            GraphBuffer uploads,
                                                            const ScatterCullGPUData& views)
{
    groupData_.clear();
    if (!gpuCulling_ || nextBatch_ == 0 || cullPipeline_ == VK_NULL_HANDLE)
        return std::nullopt;
    groupData_.resize(groups_.size());
    std::vector<ScatterDrawCommandGPUData> groupCommands(groups_.size());
    uint64_t mainRoom = 0;
    uint64_t shadowRoom = 0;
    for (const Group& g : groups_)
    {
        mainRoom += g.instances_;
        shadowRoom += g.shadowInstances_;
    }
    mainRoom = std::min(mainRoom, visibleCapacity_ / 2);
    shadowRoom =
        std::min(shadowRoom, (visibleCapacity_ - 2 * mainRoom) / (ScatterCullViews - 1));
    uint64_t offset = 0;
    uint64_t shadowOffset = 0;
    for (size_t g = 0; g < groups_.size(); ++g)
    {
        ScatterDrawCommandGPUData& command = groupCommands[g];
        if (g == ScatterImpostorGroup)
        {
            command.indexCount_ = 6;
        }
        else
        {
            const Mesh* mesh = draw.assetManager_->get(groups_[g].mesh_);
            if (!mesh || !mesh->isRenderable())
                continue;
            if (mesh->subMeshCount == 0)
            {
                command.indexCount_ = mesh->indexCount_;
            }
            else
            {
                const auto& sub = mesh->subMeshes[groups_[g].submesh_];
                command.indexCount_ = sub.indexCount;
                command.firstIndex_ = sub.indexOffset;
            }
        }
        ScatterDrawGroupGPUData& data = groupData_[g];
        if (offset + groups_[g].instances_ <= mainRoom)
        {
            data.offset_ = static_cast<uint32_t>(offset);
            data.capacity_ = static_cast<uint32_t>(groups_[g].instances_);
            offset += groups_[g].instances_;
        }
        if (shadowOffset + groups_[g].shadowInstances_ <= shadowRoom)
        {
            data.shadowOffset_ = static_cast<uint32_t>(shadowOffset);
            data.shadowCapacity_ = static_cast<uint32_t>(groups_[g].shadowInstances_);
            shadowOffset += groups_[g].shadowInstances_;
        }
    }
    for (ScatterDrawGroupGPUData& data : groupData_)
        data.shadowOffset_ += static_cast<uint32_t>(2 * offset);
    shadowStride_ = static_cast<uint32_t>(shadowOffset);
    mainStride_ = static_cast<uint32_t>(offset);
    hizActive_ = hiz_ && hzbLevels_ > 0 && lateCullPipeline_ != VK_NULL_HANDLE &&
                 hzbPipeline_ != VK_NULL_HANDLE;
    ScatterCullGPUData cull = views;
    cull.shadowStride_ = shadowStride_;
    cull.mainStride_ = mainStride_;
    cull.screenWidth_ = extent_.width;
    cull.screenHeight_ = extent_.height;
    cull.hzbLevels_ = hzbLevels_;
    cull.hiz_ = hizActive_ ? 1u : 0u;

    const GraphBuffer table = graph.importBuffer("scatter draw groups", setup_.resources_,
                                                 groupBuffer_, Usage::None, Usage::None);
    const GraphBuffer commandBuffer = graph.importBuffer(
        "scatter draw commands", setup_.resources_, commands_, Usage::None, Usage::None);
    const GraphBuffer visible = graph.importBuffer("scatter visible instances", setup_.resources_,
                                                   visible_, Usage::None, Usage::None);
    const GraphBuffer cullViews = graph.importBuffer("scatter cull views", setup_.resources_,
                                                     cullData_, Usage::None, Usage::None);
    const GraphBuffer visibility =
        graph.importBuffer("scatter batch visibility", setup_.resources_, visibility_,
                           Usage::ComputeWrite, Usage::ComputeWrite);
    graph.addPass("scatter cull setup")
        .write(table, Usage::TransferDst)
        .write(commandBuffer, Usage::TransferDst)
        .write(cullViews, Usage::TransferDst)
        .execute(
            [table, commandBuffer, cullViews, cull, data = groupData_,
             commands = std::move(groupCommands)](GraphContext& c)
            {
                vkCmdUpdateBuffer(c.cmd(), c.buffer(table), 0,
                                  data.size() * sizeof(ScatterDrawGroupGPUData), data.data());
                vkCmdUpdateBuffer(c.cmd(), c.buffer(cullViews), 0, sizeof cull, &cull);
                for (uint32_t view = 0; view < ScatterCommandViews; ++view)
                    vkCmdUpdateBuffer(c.cmd(), c.buffer(commandBuffer),
                                      uint64_t{view} * ScatterMaxDrawGroups * kCommandBytes,
                                      commands.size() * kCommandBytes, commands.data());
            });
    graph.addPass("scatter cull")
        .read(table, Usage::ShaderRead)
        .read(cullViews, Usage::ShaderRead)
        .read(visibility, Usage::ShaderRead)
        .read(uploads, Usage::AnyRead)
        .write(commandBuffer, Usage::ComputeWrite)
        .write(visible, Usage::ComputeWrite)
        .execute(
            [draw, pipeline = cullPipeline_, batches = nextBatch_](GraphContext& c)
            {
                const DrawContext bound = bindCompute(draw, c.cmd());
                vkCmdBindPipeline(bound.cmd_, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
                DrawPush push = bound.push_;
                push.instanceIndex_ = batches;
                pushDraw(bound, push);
                vkCmdDispatch(bound.cmd_, std::min(batches, ScatterCullRow),
                              (batches + ScatterCullRow - 1) / ScatterCullRow, 1);
            });
    return ScatterCullBuffers{commandBuffer, visible, table, cullViews, visibility, hizActive_};
}

void ScatterDrawer::prepareHzb(VkExtent2D extent)
{
    if (extent.width == extent_.width && extent.height == extent_.height)
        return;
    extent_ = extent;
    hzbLevels_ = 0;
    if (extent.width == 0 || extent.height == 0)
        return;
    uint64_t width = (uint64_t{extent.width} + 1) / 2;
    uint64_t height = (uint64_t{extent.height} + 1) / 2;
    uint64_t texels = width * height;
    hzbLevels_ = 1;
    while (width > 1 || height > 1)
    {
        width = (width + 1) / 2;
        height = (height + 1) / 2;
        texels += width * height;
        ++hzbLevels_;
    }
    if (texels <= hzbCapacity_)
        return;
    setup_.resources_.requestDestroy(hzb_);
    hzbCapacity_ = texels;
    hzb_ = setup_.resources_.createPerFrameBuffer(sizeof(float) * hzbCapacity_);
}

bool ScatterDrawer::addOcclusionPasses(RenderGraph& graph, const DrawContext& draw,
                                       const ScatterCullBuffers& culled, GraphImage depth,
                                       GraphBuffer uploads)
{
    if (!culled.hiz_)
        return false;
    const GraphBuffer hzb =
        graph.importBuffer("scatter hzb", setup_.resources_, hzb_, Usage::None, Usage::None);
    uint32_t width = (extent_.width + 1) / 2;
    uint32_t height = (extent_.height + 1) / 2;
    for (uint32_t level = 0; level < hzbLevels_; ++level)
    {
        GraphPass& pass = graph.addPass("scatter hzb " + std::to_string(level))
                              .read(culled.cullViews_, Usage::ShaderRead)
                              .write(hzb, Usage::ComputeWrite);
        if (level == 0)
            pass.read(depth, Usage::ShaderRead);
        pass.execute(
            [draw, depth, level, width, height, pipeline = hzbPipeline_](GraphContext& c)
            {
                const DrawContext bound = bindCompute(draw, c.cmd());
                vkCmdBindPipeline(bound.cmd_, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
                DrawPush push = bound.push_;
                push.instanceIndex_ = level;
                if (level == 0)
                    push.submeshIndex_ = c.textureIndex(depth);
                pushDraw(bound, push);
                vkCmdDispatch(bound.cmd_, (width + 7) / 8, (height + 7) / 8, 1);
            });
        width = (width + 1) / 2;
        height = (height + 1) / 2;
    }
    graph.addPass("scatter cull late")
        .read(culled.table_, Usage::ShaderRead)
        .read(culled.cullViews_, Usage::ShaderRead)
        .read(hzb, Usage::ShaderRead)
        .read(uploads, Usage::AnyRead)
        .write(culled.visibility_, Usage::ComputeWrite)
        .write(culled.commands_, Usage::ComputeWrite)
        .write(culled.visible_, Usage::ComputeWrite)
        .execute(
            [draw, pipeline = lateCullPipeline_, batches = nextBatch_](GraphContext& c)
            {
                const DrawContext bound = bindCompute(draw, c.cmd());
                vkCmdBindPipeline(bound.cmd_, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
                DrawPush push = bound.push_;
                push.instanceIndex_ = batches;
                pushDraw(bound, push);
                vkCmdDispatch(bound.cmd_, std::min(batches, ScatterCullRow),
                              (batches + ScatterCullRow - 1) / ScatterCullRow, 1);
            });
    return true;
}

void ScatterDrawer::recordIndirect(const DrawContext& draw, VkPipeline meshPipeline,
                                   VkPipeline impostorPipeline, uint32_t streams,
                                   uint32_t view) const
{
    const VkBuffer commands = setup_.resources_.bufferFor(commands_);
    const bool main = view == 0 || view == ScatterLateView;
    const auto capacityOf = [&](size_t g)
    { return main ? groupData_[g].capacity_ : groupData_[g].shadowCapacity_; };
    DrawPush push = draw.push_;
    const auto drawGroup = [&](size_t g)
    {
        const ScatterDrawGroupGPUData& data = groupData_[g];
        push.instanceIndex_ = data.shadowOffset_ + (view - 1) * shadowStride_;
        if (view == 0)
            push.instanceIndex_ = data.offset_;
        else if (view == ScatterLateView)
            push.instanceIndex_ = data.offset_ + mainStride_;
        push.submeshIndex_ = groups_[g].submesh_;
        pushDraw(draw, push);
        vkCmdDrawIndexedIndirect(draw.cmd_, commands,
                                 (uint64_t{view} * ScatterMaxDrawGroups + g) * kCommandBytes, 1,
                                 static_cast<uint32_t>(kCommandBytes));
    };
    if (meshPipeline != VK_NULL_HANDLE)
    {
        bool bound = false;
        for (size_t g = 1; g < groupData_.size(); ++g)
        {
            if (capacityOf(g) == 0)
                continue;
            const Mesh* mesh = draw.assetManager_->get(groups_[g].mesh_);
            if (!mesh || !mesh->isRenderable())
                continue;
            if (!bound)
            {
                vkCmdBindPipeline(draw.cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, meshPipeline);
                bound = true;
            }
            const VkBuffer meshBuffer = setup_.resources_.bufferFor(mesh->buffer_);
            const std::array<VkBuffer, Mesh::VertexStreams> vertexBuffers{meshBuffer, meshBuffer,
                                                                          meshBuffer, meshBuffer};
            vkCmdBindVertexBuffers(draw.cmd_, 0, streams, vertexBuffers.data(),
                                   mesh->streamOffsets_.data());
            vkCmdBindIndexBuffer(draw.cmd_, meshBuffer, mesh->streamOffsets_[Mesh::Index],
                                 toVkIndexType(mesh->indexFormat_));
            drawGroup(g);
        }
    }
    if (impostorPipeline != VK_NULL_HANDLE && !groupData_.empty() &&
        capacityOf(ScatterImpostorGroup) > 0)
    {
        vkCmdBindPipeline(draw.cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, impostorPipeline);
        vkCmdBindIndexBuffer(draw.cmd_, setup_.resources_.bufferFor(quadIndices_), 0,
                             VK_INDEX_TYPE_UINT32);
        drawGroup(ScatterImpostorGroup);
    }
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
    push.instanceIndex_ = InvalidGPUIndex;
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

        if (mesh->subMeshCount == 0)
        {
            push.submeshIndex_ = 0;
            pushDraw(draw, push);
            vkCmdDrawIndexed(draw.cmd_, mesh->indexCount_, d.count_, 0, 0, d.first_);
            continue;
        }
        for (uint8_t sub = 0; sub < mesh->subMeshCount; ++sub)
        {
            push.submeshIndex_ = sub;
            pushDraw(draw, push);
            const auto& subMesh = mesh->subMeshes[sub];
            vkCmdDrawIndexed(draw.cmd_, subMesh.indexCount, d.count_, subMesh.indexOffset, 0,
                             d.first_);
        }
    }
}

void ScatterDrawer::recordPrepass(const DrawContext& draw)
{
    if (gpuCulling_ && !groupData_.empty())
    {
        recordIndirect(draw, draw.reflections_ ? reflectPrepassPipeline_ : prepassPipeline_,
                       draw.reflections_ ? impostorReflectPrepassPipeline_
                                         : impostorPrepassPipeline_,
                       Mesh::VertexStreams, 0);
        return;
    }
    recordDraws(draw, draw.reflections_ ? reflectPrepassPipeline_ : prepassPipeline_,
                Mesh::VertexStreams, nullptr);
    recordImpostors(draw,
                    draw.reflections_ ? impostorReflectPrepassPipeline_ : impostorPrepassPipeline_);
}

void ScatterDrawer::recordPrepassLate(const DrawContext& draw)
{
    if (!gpuCulling_ || groupData_.empty() || !hizActive_)
        return;
    recordIndirect(draw, draw.reflections_ ? reflectPrepassPipeline_ : prepassPipeline_,
                   draw.reflections_ ? impostorReflectPrepassPipeline_ : impostorPrepassPipeline_,
                   Mesh::VertexStreams, ScatterLateView);
}

void ScatterDrawer::record(const DrawContext& draw)
{
    if (gpuCulling_ && !groupData_.empty())
    {
        for (const uint32_t view : {0u, ScatterLateView})
            if (view == 0 || hizActive_)
                recordIndirect(draw, draw.rayTracing_ ? rtPipeline_ : pipeline_,
                               draw.rayTracing_ ? impostorRtPipeline_ : impostorPipeline_,
                               Mesh::VertexStreams, view);
        return;
    }
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
    push.instanceIndex_ = InvalidGPUIndex;
    for (const Draw& d : draws_)
    {
        if (!d.impostor_ || !d.visible_)
            continue;
        if (!bound)
        {
            vkCmdBindPipeline(draw.cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            pushDraw(draw, push);
            bound = true;
        }
        vkCmdDraw(draw.cmd_, 6, d.count_, 0, d.first_);
    }
}

void ScatterDrawer::recordShadow(const DrawContext& draw, const ScatterShadowView& view)
{
    if (gpuCulling_ && !groupData_.empty())
    {
        recordIndirect(draw, shadowPipeline_, VK_NULL_HANDLE, 1, view.cullView_);
        return;
    }
    recordDraws(draw, shadowPipeline_, 1, &view);
}

bool ScatterDrawer::hasShadowCasters() const
{
    if (gpuCulling_)
        return std::ranges::any_of(groups_,
                                   [](const Group& g) { return g.shadowInstances_ > 0; });
    return std::ranges::any_of(draws_, [](const Draw& d) { return d.castShadows_; });
}

}  // namespace batap
