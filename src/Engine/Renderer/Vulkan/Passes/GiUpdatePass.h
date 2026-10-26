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
    void resize(v2i frameSize);
    void addPasses(RenderGraph& graph, const RtSceneBuffers& rt, GraphBuffer uploads,
                   GraphBuffer frameConstants, const PassContext& pass, uint32_t frame,
                   uint32_t passes);

    v2i size() const { return size_; }
    float gpuMs() const { return gpuMs_; }

   private:
    PassSetup setup_;
    VkPipeline updatePipeline_ = VK_NULL_HANDLE;
    VkPipeline resolvePipeline_ = VK_NULL_HANDLE;
    GPUResourceHandle updateTarget_;
    GPUResourceHandle resolveTarget_;
    v2i size_ = v2i::Zero();
    VkQueryPool queries_ = VK_NULL_HANDLE;
    std::array<bool, FramesInFlight> written_{};
    float gpuMs_ = -1.f;
};
}  // namespace batap
