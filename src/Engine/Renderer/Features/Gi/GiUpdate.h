#pragma once

#include "Renderer/Device/ShaderLibrary.h"
#include "Renderer/Draws/DrawContext.h"
#include "Renderer/EngineConfig.h"
#include "Renderer/Features/Gi/GiCache.h"
#include "Renderer/Graph/RenderGraph.h"

#include <array>
#include <cstdint>

namespace batap
{
struct GiUpdate
{
    explicit GiUpdate(const DrawSetup& setup) : setup_(setup) {}
    ~GiUpdate();

    GiUpdate(const GiUpdate&) = delete;
    GiUpdate& operator=(const GiUpdate&) = delete;

    void buildPipelines(const ShaderModules& modules);
    void writeFrameData(FrameGPUData& data, VkExtent2D output, float dt, uint32_t bounces,
                        uint32_t passes);
    void addPasses(RenderGraph& graph, GraphBuffer tlas, GraphBuffer blases, GraphBuffer traced,
                   const GiCache::Buffers& cache, GraphBuffer uploads, GraphBuffer frameConstants,
                   const DrawContext& draw, uint32_t frame, VkExtent2D output);
    void report(const GiCache::Stats& stats, bool show) const;

   private:
    static VkExtent2D updateExtent(VkExtent2D output);

    DrawSetup setup_;
    VkPipeline updatePipeline_ = VK_NULL_HANDLE;
    VkPipeline resolvePipeline_ = VK_NULL_HANDLE;
    VkQueryPool queries_ = VK_NULL_HANDLE;
    std::array<bool, FramesInFlight> written_{};
    float gpuMs_ = -1.f;
    uint32_t frame_ = 0;
    uint32_t passes_ = 1;
};
}  // namespace batap
