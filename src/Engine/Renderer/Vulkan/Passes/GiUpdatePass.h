#pragma once

#include "EigenTypes.h"
#include "Handles.h"
#include "Renderer/EngineConfig.h"
#include "Renderer/Vulkan/Passes/Pass.h"
#include "Renderer/Vulkan/Passes/ShaderCatalog.h"
#include "Renderer/Vulkan/RenderGraph.h"

#include <array>

namespace batap
{
struct RtSceneBuffers;

struct GiUpdatePass
{
    explicit GiUpdatePass(const PassSetup& setup) : setup_(setup) {}
    ~GiUpdatePass();

    GiUpdatePass(const GiUpdatePass&) = delete;
    GiUpdatePass& operator=(const GiUpdatePass&) = delete;

    void buildPipelines(const ShaderModules& modules);
    static VkExtent2D updateExtent(VkExtent2D output);
    void addPasses(RenderGraph& graph, const RtSceneBuffers& rt, GraphBuffer uploads,
                   GraphBuffer frameConstants, const PassContext& pass, uint32_t frame,
                   uint32_t passes, VkExtent2D output);

    float gpuMs() const { return gpuMs_; }

   private:
    PassSetup setup_;
    VkPipeline updatePipeline_ = VK_NULL_HANDLE;
    VkPipeline resolvePipeline_ = VK_NULL_HANDLE;
    VkQueryPool queries_ = VK_NULL_HANDLE;
    std::array<bool, FramesInFlight> written_{};
    float gpuMs_ = -1.f;
};
}  // namespace batap
