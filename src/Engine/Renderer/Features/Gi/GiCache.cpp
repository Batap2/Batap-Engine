#include "Renderer/Features/Gi/GiCache.h"

#include "Renderer/Device/ResourceManager.h"
#include "Shaders/ShaderInterop.h"

#include <cstring>
#include <optional>
#include <span>

namespace batap
{
namespace
{
constexpr uint64_t kStatsBytes = sizeof(uint32_t) * RtCacheStatCount * RtCacheStatsBuckets;
}

GiCache::GiCache(ResourceManager& resources) : resources_(resources)
{
    keys_ = resources_.createStaticBuffer(uint64_t{RtCacheCapacity} * sizeof(uint64_t),
                                          "gi cache keys");
    values_ = resources_.createStaticBuffer(uint64_t{RtCacheCapacity} * sizeof(RtCacheValueGPUData),
                                            "gi cache values");
    stats_ = resources_.createStaticBuffer(kStatsBytes, "gi cache stats");
    for (GPUResourceHandle& readback : readback_)
        readback = resources_.createMappedBuffer(kStatsBytes, std::nullopt);
}

GiCache::Buffers GiCache::addPasses(RenderGraph& graph)
{
    Buffers buffers;
    buffers.keys_ = graph.importBuffer("gi cache keys", resources_, keys_, Usage::ShaderRead,
                                       Usage::ShaderRead);
    buffers.values_ = graph.importBuffer("gi cache values", resources_, values_, Usage::ShaderRead,
                                         Usage::ShaderRead);
    buffers.stats_ = graph.importBuffer("gi cache stats", resources_, stats_, Usage::TransferSrc,
                                        Usage::TransferSrc);
    if (clear_)
    {
        graph.addPass("gi cache clear")
            .write(buffers.keys_, Usage::TransferDst)
            .write(buffers.values_, Usage::TransferDst)
            .execute(
                [keys = buffers.keys_, values = buffers.values_](GraphContext& c)
                {
                    vkCmdFillBuffer(c.cmd(), c.buffer(keys), 0, VK_WHOLE_SIZE, 0u);
                    vkCmdFillBuffer(c.cmd(), c.buffer(values), 0, VK_WHOLE_SIZE, 0u);
                });
        clear_ = false;
    }
    graph.addPass("gi cache stats reset")
        .write(buffers.stats_, Usage::TransferDst)
        .execute([stats = buffers.stats_](GraphContext& c)
                 { vkCmdFillBuffer(c.cmd(), c.buffer(stats), 0, VK_WHOLE_SIZE, 0u); });
    return buffers;
}

GiCache::Stats GiCache::addStatsPass(RenderGraph& graph, const Buffers& buffers, uint32_t frame)
{
    Stats stats;
    const std::span<const std::byte> mapped = resources_.mappedFor(readback_[frame]);
    std::array<uint32_t, RtCacheStatCount * RtCacheStatsBuckets> counters{};
    std::memcpy(counters.data(), mapped.data(), sizeof(counters));
    for (uint32_t bucket = 0; bucket < RtCacheStatsBuckets; ++bucket)
    {
        stats.occupied_ += counters[RtCacheStatOccupied * RtCacheStatsBuckets + bucket];
        stats.inserted_ += counters[RtCacheStatInserted * RtCacheStatsBuckets + bucket];
        stats.failed_ += counters[RtCacheStatFailed * RtCacheStatsBuckets + bucket];
        stats.evicted_ += counters[RtCacheStatEvicted * RtCacheStatsBuckets + bucket];
        stats.tailMiss_ += counters[RtCacheStatTailMiss * RtCacheStatsBuckets + bucket];
    }

    const GraphBuffer readback = graph.importBuffer("gi cache stats readback", resources_,
                                                    readback_[frame], Usage::None, Usage::None);
    graph.addPass("gi cache stats")
        .read(buffers.stats_, Usage::TransferSrc)
        .write(readback, Usage::TransferDst)
        .execute(
            [statsBuffer = buffers.stats_, readback](GraphContext& c)
            {
                VkBufferCopy copy{};
                copy.size = kStatsBytes;
                vkCmdCopyBuffer(c.cmd(), c.buffer(statsBuffer), c.buffer(readback), 1, &copy);
            });
    return stats;
}
}  // namespace batap
