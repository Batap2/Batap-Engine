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
#include <cstring>
#include <iostream>
#include <span>

namespace batap
{
namespace
{
constexpr uint32_t kMaxBatches = 4096;
constexpr uint64_t kInitialInstances = 4096;
constexpr uint64_t kRetireFrames = FramesInFlight + 1;
}  // namespace

ScatterDrawer::ScatterDrawer(const DrawSetup& setup) : setup_(setup)
{
    batches_ = setup_.resources_.createPerFrameBuffer(sizeof(ScatterBatchGPUData) * kMaxBatches,
                                                      "scatter batches");
    instances_ =
        setup_.resources_.createStaticBuffer(sizeof(ScatterInstanceGPUData) * kInitialInstances);
    capacity_ = kInitialInstances;
}

ScatterDrawer::~ScatterDrawer()
{
    vkDestroyPipeline(setup_.ctx_.device_, prepassPipeline_, nullptr);
    vkDestroyPipeline(setup_.ctx_.device_, pipeline_, nullptr);
    vkDestroyPipeline(setup_.ctx_.device_, rtPipeline_, nullptr);
    vkDestroyPipeline(setup_.ctx_.device_, shadowPipeline_, nullptr);
    setup_.resources_.requestDestroy(batches_);
    setup_.resources_.requestDestroy(instances_);
    for (const auto& [frame, buffer] : retiredBuffers_)
        setup_.resources_.requestDestroy(buffer);
}

void ScatterDrawer::buildPipelines(const ShaderModules& modules)
{
    vkDestroyPipeline(setup_.ctx_.device_, prepassPipeline_, nullptr);
    vkDestroyPipeline(setup_.ctx_.device_, pipeline_, nullptr);
    vkDestroyPipeline(setup_.ctx_.device_, rtPipeline_, nullptr);
    vkDestroyPipeline(setup_.ctx_.device_, shadowPipeline_, nullptr);

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
        for (size_t i = 0; i < slot.materials_.size(); ++i)
            slot.materials_[i] = batch.materials_[i] ? batch.materials_[i].index : InvalidGPUIndex;
        slot.castShadows_ = batch.castShadows_;
        slot.fadeStart_ = batch.fadeStart_;
        slot.fadeEnd_ = batch.fadeEnd_;
        slot.count_ = batch.instances_.size();
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

    batchData_.clear();
    draws_.clear();
    for (const auto& [id, placement] : draws.placed_)
    {
        const auto it = slots_.find(id);
        if (it == slots_.end() || it->second.count_ == 0)
            continue;
        if (batchData_.size() >= kMaxBatches)
        {
            if (!warnedFull_)
                std::cerr << "[Scatter] more than " << kMaxBatches
                          << " batches, the rest not drawn\n";
            warnedFull_ = true;
            break;
        }
        Slot& slot = it->second;
        ScatterBatchGPUData& gpu = batchData_.emplace_back();
        flatten(gpu.placed_.world_, placement.world_);
        const bool placedBefore = slot.placedFrame_ != 0 && slot.placedFrame_ + 1 == frame_;
        flatten(gpu.placed_.prevWorld_, placedBefore ? slot.placedWorld_ : placement.world_);
        slot.placedWorld_ = placement.world_;
        slot.placedFrame_ = frame_;
        const v3d cells = (placement.centreInOwner_ / double(GiCellBase)).array().floor();
        const v3i cell = cells.cast<int>();
        std::memcpy(gpu.placed_.centreCell_, cell.data(), sizeof gpu.placed_.centreCell_);
        flatten(gpu.placed_.centreOffset_,
                (placement.centreInOwner_ - cells * double(GiCellBase)).cast<float>());
        gpu.placed_.owner_ = placement.owner_;
        gpu.placed_.tracedBase_ = TerrainNoTraced;
        std::copy(slot.materials_.begin(), slot.materials_.end(),
                  std::span{gpu.materialIndices_}.begin());
        gpu.firstInstance_ = static_cast<uint32_t>(slot.first_);
        gpu.fadeStart_ = slot.fadeStart_;
        gpu.fadeEnd_ = slot.fadeEnd_;
        draws_.push_back({slot.mesh_, static_cast<uint32_t>(batchData_.size() - 1),
                          static_cast<uint32_t>(slot.count_), slot.castShadows_,
                          placement.visible_});
    }
    if (!batchData_.empty())
    {
        const uint64_t bytes = sizeof(ScatterBatchGPUData) * batchData_.size();
        std::memcpy(setup_.resources_.requestUpload(batches_, bytes).data(), batchData_.data(),
                    bytes);
    }
}

void ScatterDrawer::recordDraws(const DrawContext& draw, VkPipeline pipeline, uint32_t streams,
                                bool castersOnly) const
{
    if (draws_.empty() || pipeline == VK_NULL_HANDLE)
        return;
    vkCmdBindPipeline(draw.cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    DrawPush push = draw.push_;
    for (const Draw& d : draws_)
    {
        if (castersOnly ? !d.castShadows_ : !d.visible_)
            continue;
        const Mesh* mesh = d.mesh_ ? draw.assetManager_->get(d.mesh_) : nullptr;
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
    recordDraws(draw, prepassPipeline_, Mesh::VertexStreams, false);
}

void ScatterDrawer::record(const DrawContext& draw)
{
    recordDraws(draw, draw.rayTracing_ ? rtPipeline_ : pipeline_, Mesh::VertexStreams, false);
}

void ScatterDrawer::recordShadow(const DrawContext& draw)
{
    recordDraws(draw, shadowPipeline_, 1, true);
}

}  // namespace batap
