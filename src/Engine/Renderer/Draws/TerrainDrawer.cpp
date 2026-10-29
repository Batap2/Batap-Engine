#include "Renderer/Draws/TerrainDrawer.h"

#include "Flatten.h"
#include "Renderer/Device/Formats.h"
#include "Renderer/Device/Pipelines.h"
#include "Renderer/Device/ResourceManager.h"
#include "Renderer/Device/Usage.h"
#include "Renderer/Device/VulkanContext.h"
#include "Renderer/EngineConfig.h"
#include "Renderer/ResourceFormat.h"
#include "Renderer/TerrainDraws.h"

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
constexpr uint32_t kMaxMorphRows = 16384;
}  // namespace

TerrainDrawer::TerrainDrawer(const DrawSetup& setup) : setup_(setup)
{
    chunks_ = setup_.resources_.createStaticBuffer(sizeof(TerrainChunkGPUData) * kMaxChunks);
    worlds_ = setup_.resources_.createPerFrameBuffer(sizeof(TerrainWorldGPUData) * kMaxChunks,
                                                     "terrain worlds");
    morphRows_ = setup_.resources_.createPerFrameBuffer(
        sizeof(TerrainMorphRowGPUData) * kMaxMorphRows, "terrain morph rows");
    vertices_.stride_ = sizeof(TerrainVertexGPUData);
    indices_.stride_ = sizeof(uint16_t);
    traced_.stride_ = sizeof(float) * 3;
    reserve(vertices_, kInitialChunks * kChunkVertices);
    reserve(indices_, kInitialChunks * kChunkIndices);
    reserve(traced_, kInitialChunks * kChunkVertices);
}

TerrainDrawer::~TerrainDrawer()
{
    vkDestroyPipeline(setup_.ctx_.device_, pipeline_, nullptr);
    vkDestroyPipeline(setup_.ctx_.device_, rtPipeline_, nullptr);
    vkDestroyPipeline(setup_.ctx_.device_, shadowPipeline_, nullptr);
    vkDestroyPipeline(setup_.ctx_.device_, morphPipeline_, nullptr);
    setup_.resources_.requestDestroy(chunks_);
    setup_.resources_.requestDestroy(worlds_);
    setup_.resources_.requestDestroy(morphRows_);
    setup_.resources_.requestDestroy(vertices_.buffer_);
    setup_.resources_.requestDestroy(indices_.buffer_);
    setup_.resources_.requestDestroy(traced_.buffer_);
    for (const auto& [frame, buffer] : retiredBuffers_)
        setup_.resources_.requestDestroy(buffer);
}

void TerrainDrawer::buildPipelines(const ShaderModules& modules)
{
    vkDestroyPipeline(setup_.ctx_.device_, pipeline_, nullptr);
    vkDestroyPipeline(setup_.ctx_.device_, rtPipeline_, nullptr);
    vkDestroyPipeline(setup_.ctx_.device_, shadowPipeline_, nullptr);
    vkDestroyPipeline(setup_.ctx_.device_, morphPipeline_, nullptr);

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

    // Shadows' own settings.
    shadowPipeline_ = GraphicsPipelineBuilder()
                          .shaders(modules[TerrainShadowVS], VK_NULL_HANDLE)
                          .depth(VK_FORMAT_D32_SFLOAT, true, VK_COMPARE_OP_LESS)
                          .depthOnly()
                          .depthClamp()
                          .cullFront()
                          .build(setup_.ctx_.device_, setup_.layout_);

    morphPipeline_ = GraphicsPipelineBuilder()
                         .shaders(modules[SkyVS], modules[TerrainMorphPS])
                         .colorFormat(VK_FORMAT_R8_UNORM)
                         .build(setup_.ctx_.device_, setup_.layout_);
}

void TerrainDrawer::reserve(Pool& pool, uint64_t elements)
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

uint64_t TerrainDrawer::allocate(Pool& pool, uint64_t elements)
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

std::optional<uint32_t> TerrainDrawer::allocateInstance()
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

void TerrainDrawer::writeChunk(uint32_t instance, const Slot& slot,
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

void TerrainDrawer::upload(TerrainDraws& draws, bool tracing)
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
                      traced_.free_[slot.vertices_].push_back(slot.traced_);
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
    reserve(traced_, traced_.end_ + vertexNeed - vertices_.end_);

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
        slot.morphs_ = mesh.params_.morphs_;
        slot.vertex_ = allocate(vertices_, slot.vertices_);
        slot.index_ = allocate(indices_, slot.indices_);
        slot.traced_ = allocate(traced_, slot.vertices_);

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
        toMorph_.insert(id);
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
        slot.morphs_ = params.morphs_;
        toMorph_.insert(id);
        drawsDirty_ = drawsDirty_ || slot.shown_;
    }
    draws.paramChanges_.clear();

    for (const auto& [id, shown] : draws.shownChanges_)
        if (const auto it = slots_.find(id); it != slots_.end() && it->second.shown_ != shown)
        {
            it->second.shown_ = shown;
            if (shown)
                toMorph_.insert(id);
            drawsDirty_ = true;
        }
    draws.shownChanges_.clear();

    worldData_.resize(instanceEnd_);
    for (const auto& [id, placement] : draws.placed_)
        if (const auto it = slots_.find(id); it != slots_.end())
        {
            TerrainWorldGPUData& gpu = worldData_[it->second.instance_];
            flatten(gpu.world_, placement.world_);
            const v3d cells = (placement.centreInOwner_ / double(GiCellBase)).array().floor();
            const v3i cell = cells.cast<int>();
            std::memcpy(gpu.centreCell_, cell.data(), sizeof gpu.centreCell_);
            flatten(gpu.centreOffset_,
                    (placement.centreInOwner_ - cells * double(GiCellBase)).cast<float>());
            gpu.owner_ = placement.owner_;
            if (placement.moved_ && it->second.morphs_)
                toMorph_.insert(id);
        }
    if (instanceEnd_ > 0)
    {
        const uint64_t bytes = sizeof(TerrainWorldGPUData) * instanceEnd_;
        std::memcpy(setup_.resources_.requestUpload(worlds_, bytes).data(), worldData_.data(),
                    bytes);
        stagedBytes_ += bytes;
    }
    planMorphs(tracing);

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

void TerrainDrawer::planMorphs(bool tracing)
{
    morphed_.clear();
    morphRowData_.clear();
    if (tracing && !tracing_)
        for (const auto& [id, slot] : slots_)
            toMorph_.insert(id);
    tracing_ = tracing;
    if (!tracing)
    {
        toMorph_.clear();
        return;
    }
    for (auto it = toMorph_.begin(); it != toMorph_.end();)
    {
        const auto slot = slots_.find(*it);
        if (slot == slots_.end())
        {
            it = toMorph_.erase(it);
            continue;
        }
        if (!slot->second.shown_)
        {
            ++it;
            continue;
        }
        const auto vertices = static_cast<uint32_t>(slot->second.vertices_);
        const uint32_t rows = (vertices + TerrainMorphRowWidth - 1) / TerrainMorphRowWidth;
        if (morphRowData_.size() + rows > kMaxMorphRows)
            break;
        for (uint32_t row = 0; row < rows; ++row)
            morphRowData_.push_back({slot->second.instance_, row * TerrainMorphRowWidth, vertices,
                                     static_cast<uint32_t>(slot->second.traced_)});
        slot->second.tracedReady_ = true;
        morphed_.push_back(*it);
        it = toMorph_.erase(it);
    }
    if (morphRowData_.empty())
        return;
    const uint64_t bytes = sizeof(TerrainMorphRowGPUData) * morphRowData_.size();
    std::memcpy(setup_.resources_.requestUpload(morphRows_, bytes).data(), morphRowData_.data(),
                bytes);
    stagedBytes_ += bytes;
}

GraphBuffer TerrainDrawer::addMorphPass(RenderGraph& graph, const DrawContext& draw,
                                        GraphBuffer uploads)
{
    const GraphBuffer traced = graph.importBuffer(
        "terrain traced", setup_.resources_, traced_.buffer_, Usage::ShaderRead, Usage::ShaderRead);
    if (morphRowData_.empty() || morphPipeline_ == VK_NULL_HANDLE)
        return traced;
    const GraphImage target =
        graph.createImage("terrain morph target",
                          {{TerrainMorphRowWidth, static_cast<uint32_t>(morphRowData_.size())},
                           toVkFormat(ResourceFormat::R8_UNORM)});
    graph.addPass("terrain morph")
        .read(uploads, Usage::AnyRead)
        .write(traced, Usage::ShaderWrite)
        .color(target, Load::DontCare)
        .execute(
            [draw, pipeline = morphPipeline_](GraphContext& c)
            {
                const DrawContext bound = bindDraw(draw, c.cmd());
                vkCmdBindPipeline(bound.cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                pushDraw(bound, bound.push_);
                vkCmdDraw(bound.cmd_, 3, 1, 0, 0);
            });
    return traced;
}

void TerrainDrawer::recordDraws(const DrawContext& draw, VkPipeline pipeline,
                                const std::vector<Draw>& draws)
{
    if (draws.empty())
        return;
    vkCmdBindPipeline(draw.cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindIndexBuffer(draw.cmd_, setup_.resources_.bufferFor(indices_.buffer_), 0,
                         VK_INDEX_TYPE_UINT16);
    // The indices are local to a chunk: vertexOffset 0, and the vertex shader
    // adds vertexBase_ itself, so SV_VertexID is the grid index.
    DrawPush push = draw.push_;
    for (const Draw& d : draws)
    {
        push.instanceIndex_ = d.instance_;
        push.submeshIndex_ = d.stratum_;
        pushDraw(draw, push);
        vkCmdDrawIndexed(draw.cmd_, d.count_, 1, d.firstIndex_, 0, 0);
    }
}

void TerrainDrawer::record(const DrawContext& draw)
{
    recordDraws(draw, draw.rayTracing_ ? rtPipeline_ : pipeline_, draws_);
}

void TerrainDrawer::recordShadow(const DrawContext& draw)
{
    recordDraws(draw, shadowPipeline_, shadowDraws_);
}
}  // namespace batap
