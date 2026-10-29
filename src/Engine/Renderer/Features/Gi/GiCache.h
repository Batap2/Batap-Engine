#pragma once

#include "Handles.h"
#include "Renderer/EngineConfig.h"
#include "Renderer/Graph/RenderGraph.h"

#include <array>
#include <cstdint>

namespace batap
{
struct ResourceManager;

struct GiCache
{
    explicit GiCache(ResourceManager& resources);

    GiCache(const GiCache&) = delete;
    GiCache& operator=(const GiCache&) = delete;

    struct Buffers
    {
        GraphBuffer keys_;
        GraphBuffer values_;
        GraphBuffer stats_;
        GraphBuffer work_;
        GraphBuffer probes_;
    };

    struct Stats
    {
        uint32_t occupied_ = 0;
        uint32_t inserted_ = 0;
        uint32_t failed_ = 0;
        uint32_t evicted_ = 0;
        uint32_t tailMiss_ = 0;
    };

    Buffers addPasses(RenderGraph& graph);
    Stats addStatsPass(RenderGraph& graph, const Buffers& buffers, uint32_t frame);

    GPUResourceHandle keys() const { return keys_; }
    GPUResourceHandle values() const { return values_; }
    GPUResourceHandle stats() const { return stats_; }
    GPUResourceHandle work() const { return work_; }
    GPUResourceHandle probes() const { return probes_; }

   private:
    ResourceManager& resources_;
    GPUResourceHandle keys_;
    GPUResourceHandle values_;
    GPUResourceHandle stats_;
    GPUResourceHandle work_;
    GPUResourceHandle probes_;
    std::array<GPUResourceHandle, FramesInFlight> readback_;
    bool clear_ = true;
};
}  // namespace batap
