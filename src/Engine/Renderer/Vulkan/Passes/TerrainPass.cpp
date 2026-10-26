#include "Renderer/Vulkan/Passes/TerrainPass.h"

#include "Flatten.h"
#include "Renderer/EngineConfig.h"
#include "Renderer/TerrainDraws.h"
#include "Renderer/Vulkan/VulkanContext.h"
#include "Renderer/Vulkan/VulkanPipelines.h"
#include "Renderer/Vulkan/VulkanResources.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <span>

namespace batap
{
namespace
{
constexpr uint32_t kMaxChunks = 16384;
constexpr uint64_t kInitialChunks = 64;
constexpr auto kChunkVertices = static_cast<uint64_t>((kChunkCells + 1) * (kChunkCells + 1));
constexpr auto kChunkIndices = static_cast<uint64_t>(kChunkCells * kChunkCells * 6);
// A frame records the draws, the slot leaves the next one, and the frames in
// flight between may still read it.
constexpr uint64_t kRetireFrames = FramesInFlight + 1;
}  // namespace

TerrainPass::TerrainPass(const PassSetup& setup) : setup_(setup)
{
    chunks_ = setup_.resources_.createStaticBuffer(sizeof(TerrainChunkGPUData) * kMaxChunks);
    worlds_ = setup_.resources_.createPerFrameBuffer(sizeof(TerrainWorldGPUData) * kMaxChunks,
                                                     "terrain worlds");
    vertices_.stride_ = sizeof(TerrainVertexGPUData);
    indices_.stride_ = sizeof(uint16_t);
    reserve(vertices_, kInitialChunks * kChunkVertices);
    reserve(indices_, kInitialChunks * kChunkIndices);
}

TerrainPass::~TerrainPass()
{
    vkDestroyPipeline(setup_.ctx_.device_, pipeline_, nullptr);
    vkDestroyPipeline(setup_.ctx_.device_, rtPipeline_, nullptr);
    vkDestroyPipeline(setup_.ctx_.device_, shadowPipeline_, nullptr);
    setup_.resources_.requestDestroy(chunks_);
    setup_.resources_.requestDestroy(worlds_);
    setup_.resources_.requestDestroy(vertices_.buffer_);
    setup_.resources_.requestDestroy(indices_.buffer_);
    for (const auto& [frame, buffer] : retiredBuffers_)
        setup_.resources_.requestDestroy(buffer);
}

void TerrainPass::buildPipelines(const ShaderModules& modules)
{
    vkDestroyPipeline(setup_.ctx_.device_, pipeline_, nullptr);
    vkDestroyPipeline(setup_.ctx_.device_, rtPipeline_, nullptr);
    vkDestroyPipeline(setup_.ctx_.device_, shadowPipeline_, nullptr);

    const auto build = [&](VkShaderModule pixelShader, VkPipelineLayout layout)
    {
        return GraphicsPipelineBuilder()
            .shaders(modules[TerrainVS], pixelShader)
            .colorFormat(setup_.colorFormat_)
            .depth(setup_.depthFormat_, true, VK_COMPARE_OP_GREATER)
            .cullBack()
            .build(setup_.ctx_.device_, layout);
    };
    pipeline_ = build(modules[TerrainPS], setup_.layout_);
    rtPipeline_ = modules[TerrainRtPS] ? build(modules[TerrainRtPS], setup_.rtLayout_)
                                       : VK_NULL_HANDLE;

    // ShadowPass's own settings.
    shadowPipeline_ = GraphicsPipelineBuilder()
                          .shaders(modules[TerrainShadowVS], VK_NULL_HANDLE)
                          .depth(VK_FORMAT_D32_SFLOAT, true, VK_COMPARE_OP_LESS)
                          .depthOnly()
                          .depthClamp()
                          .cullFront()
                          .build(setup_.ctx_.device_, setup_.layout_);
}

void TerrainPass::reserve(Pool& pool, uint64_t elements)
{
    if (elements <= pool.capacity_)
        return;
    const uint64_t capacity = std::max(elements, pool.capacity_ * 2);
    // Unnamed: a named handle is the hash of its name, so the grown buffer
    // would come back under the old one's handle.
    const GPUResourceHandle grown = setup_.resources_.createStaticBuffer(capacity * pool.stride_);
    if (pool.buffer_.valid())
    {
        setup_.resources_.requestCopy(grown, pool.buffer_, pool.end_ * pool.stride_);
        // Not requestDestroy yet: it drops the uploads still queued for the
        // buffer, which the copy has to read.
        retiredBuffers_.emplace_back(frame_, pool.buffer_);
    }
    pool.buffer_ = grown;
    pool.capacity_ = capacity;
}

uint64_t TerrainPass::allocate(Pool& pool, uint64_t elements)
{
    if (auto it = pool.free_.find(elements); it != pool.free_.end() && !it->second.empty())
    {
        const uint64_t offset = it->second.back();
        it->second.pop_back();
        return offset;
    }
    const uint64_t offset = pool.end_;
    pool.end_ += elements;
    return offset;
}

std::optional<uint32_t> TerrainPass::allocateInstance()
{
    if (!freeInstances_.empty())
    {
        const uint32_t instance = freeInstances_.back();
        freeInstances_.pop_back();
        return instance;
    }
    if (instanceEnd_ < kMaxChunks)
        return instanceEnd_++;
    if (!warnedFull_)
    {
        std::cerr << "[Terrain] more than " << kMaxChunks << " chunks, the rest not drawn\n";
        warnedFull_ = true;
    }
    return std::nullopt;
}

void TerrainPass::writeChunk(uint32_t instance, const Slot& slot,
                             const TerrainDraws::Params& params)
{
    TerrainChunkGPUData g{};
    std::copy(params.materials_.begin(), params.materials_.end(),
              std::span{g.materialIndices_}.begin());
    g.vertexBase_ = static_cast<uint32_t>(slot.vertex_);
    g.cells_ = slot.cells_;
    g.force_ = params.force_;
    g.flags_ = (params.morphs_ ? TerrainMorphs : 0u) | (params.outline_ ? TerrainOutline : 0u);
    g.morphStart_ = params.morphStart_;
    g.morphEnd_ = params.morphEnd_;
    g.level_ = slot.level_;
    std::memcpy(
        setup_.resources_.requestUpload(chunks_, sizeof(g), uint64_t{instance} * sizeof(g)).data(),
        &g, sizeof(g));
    stagedBytes_ += sizeof(g);
}

void TerrainPass::upload(TerrainDraws& draws)
{
    const auto start = std::chrono::steady_clock::now();
    stagedBytes_ = 0;
    ++frame_;
    for (const uint64_t id : draws.removed_)
        if (const auto it = slots_.find(id); it != slots_.end())
        {
            drawsDirty_ = drawsDirty_ || it->second.shown_;
            retiredSlots_.emplace_back(frame_, it->second);
            slots_.erase(it);
        }
    draws.removed_.clear();

    std::erase_if(retiredSlots_,
                  [&](const auto& retired)
                  {
                      if (retired.first + kRetireFrames > frame_)
                          return false;
                      const Slot& slot = retired.second;
                      vertices_.free_[slot.vertices_].push_back(slot.vertex_);
                      indices_.free_[slot.indices_].push_back(slot.index_);
                      freeInstances_.push_back(slot.instance_);
                      return true;
                  });
    std::erase_if(retiredInstances_,
                  [&](const auto& retired)
                  {
                      if (retired.first + kRetireFrames > frame_)
                          return false;
                      freeInstances_.push_back(retired.second);
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

    // Grown before the first upload of the frame, so the copy carries every
    // earlier one; the free slots only make this generous.
    uint64_t vertexNeed = vertices_.end_;
    uint64_t indexNeed = indices_.end_;
    for (const auto& [id, mesh] : draws.added_)
    {
        vertexNeed += mesh.vertices_.size();
        indexNeed += mesh.indices_.size();
    }
    reserve(vertices_, vertexNeed);
    reserve(indices_, indexNeed);

    for (auto& [id, mesh] : draws.added_)
    {
        const std::optional<uint32_t> instance = allocateInstance();
        if (!instance)
            continue;
        Slot slot;
        slot.instance_ = *instance;
        slot.cells_ = mesh.cells_;
        slot.level_ = mesh.level_;
        slot.vertices_ = mesh.vertices_.size();
        slot.indices_ = mesh.indices_.size();
        slot.ranges_ = mesh.ranges_;
        slot.shown_ = mesh.shown_;
        slot.vertex_ = allocate(vertices_, slot.vertices_);
        slot.index_ = allocate(indices_, slot.indices_);

        const uint64_t vertexBytes = slot.vertices_ * vertices_.stride_;
        std::memcpy(
            setup_.resources_
                .requestUpload(vertices_.buffer_, vertexBytes, slot.vertex_ * vertices_.stride_)
                .data(),
            mesh.vertices_.data(), vertexBytes);
        const uint64_t indexBytes = slot.indices_ * indices_.stride_;
        std::memcpy(setup_.resources_
                        .requestUpload(indices_.buffer_, indexBytes, slot.index_ * indices_.stride_)
                        .data(),
                    mesh.indices_.data(), indexBytes);
        stagedBytes_ += vertexBytes + indexBytes;
        writeChunk(slot.instance_, slot, mesh.params_);
        slots_[id] = slot;
        drawsDirty_ = drawsDirty_ || slot.shown_;
    }
    draws.added_.clear();

    for (const auto& [id, params] : draws.paramChanges_)
    {
        const auto it = slots_.find(id);
        if (it == slots_.end())
            continue;
        const std::optional<uint32_t> instance = allocateInstance();
        if (!instance)
            continue;
        Slot& slot = it->second;
        writeChunk(*instance, slot, params);
        retiredInstances_.emplace_back(frame_, slot.instance_);
        slot.instance_ = *instance;
        drawsDirty_ = drawsDirty_ || slot.shown_;
    }
    draws.paramChanges_.clear();

    for (const auto& [id, shown] : draws.shownChanges_)
        if (const auto it = slots_.find(id); it != slots_.end() && it->second.shown_ != shown)
        {
            it->second.shown_ = shown;
            drawsDirty_ = true;
        }
    draws.shownChanges_.clear();

    worldData_.resize(instanceEnd_);
    for (const auto& [id, world] : draws.placed_)
        if (const auto it = slots_.find(id); it != slots_.end())
            flatten(worldData_[it->second.instance_].world_, world);
    if (instanceEnd_ > 0)
    {
        const uint64_t bytes = sizeof(TerrainWorldGPUData) * instanceEnd_;
        std::memcpy(setup_.resources_.requestUpload(worlds_, bytes).data(), worldData_.data(),
                    bytes);
        stagedBytes_ += bytes;
    }

    if (drawsDirty_)
    {
        drawsDirty_ = false;
        draws_.clear();
        shadowDraws_.clear();
        for (const auto& [id, slot] : slots_)
        {
            if (!slot.shown_)
                continue;
            for (size_t s = 0; s < kMaxTerrainStrata; ++s)
                if (slot.ranges_[s].count_ > 0)
                    draws_.push_back({slot.instance_, static_cast<uint32_t>(s),
                                      static_cast<uint32_t>(slot.index_) + slot.ranges_[s].offset_,
                                      slot.ranges_[s].count_});
            shadowDraws_.push_back({slot.instance_, 0, static_cast<uint32_t>(slot.index_),
                                    static_cast<uint32_t>(slot.indices_)});
        }
    }

    draws.lastUpload_ = {
        slots_.size(), stagedBytes_,
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
            .count()};
}

void TerrainPass::recordDraws(const PassContext& pass, VkPipeline pipeline,
                              const std::vector<Draw>& draws)
{
    if (draws.empty())
        return;
    vkCmdBindPipeline(pass.cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindIndexBuffer(pass.cmd_, setup_.resources_.bufferFor(indices_.buffer_), 0,
                         VK_INDEX_TYPE_UINT16);
    // The indices are local to a chunk: vertexOffset 0, and the vertex shader
    // adds vertexBase_ itself, so SV_VertexID is the grid index.
    DrawPush push = pass.push_;
    for (const Draw& d : draws)
    {
        push.instanceIndex_ = d.instance_;
        push.submeshIndex_ = d.stratum_;
        pushDraw(pass, push);
        vkCmdDrawIndexed(pass.cmd_, d.count_, 1, d.firstIndex_, 0, 0);
    }
}

void TerrainPass::record(const PassContext& pass)
{
    recordDraws(pass, pass.rayTracing_ ? rtPipeline_ : pipeline_, draws_);
}

void TerrainPass::recordShadow(const PassContext& pass)
{
    recordDraws(pass, shadowPipeline_, shadowDraws_);
}
}  // namespace batap
