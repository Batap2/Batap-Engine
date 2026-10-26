#pragma once

#include "Handles.h"
#include "Renderer/EngineConfig.h"
#include "Renderer/Vulkan/Passes/Pass.h"
#include "Renderer/Vulkan/Passes/ShaderCatalog.h"

#include <array>

namespace batap
{
struct Camera_C;
struct GraphBuffer;
struct GraphImage;
struct RenderGraph;

struct ExposurePass
{
    explicit ExposurePass(const PassSetup& setup);
    ~ExposurePass();

    ExposurePass(const ExposurePass&) = delete;
    ExposurePass& operator=(const ExposurePass&) = delete;

    void buildPipelines(const ShaderModules& modules);
    float update(uint32_t frame, entt::entity camera, const Camera_C& settings, float dt);
    void addPasses(RenderGraph& graph, GraphImage hdr, GraphBuffer frameConstants,
                   const PassContext& pass, uint32_t frame);

   private:
    PassSetup setup_;
    VkPipeline measurePipeline_ = VK_NULL_HANDLE;
    VkPipeline reducePipeline_ = VK_NULL_HANDLE;
    std::array<GPUResourceHandle, FramesInFlight> readback_;
    std::array<bool, FramesInFlight> measured_{};
    entt::entity camera_ = entt::null;
    bool auto_ = false;
    float ev100_ = 0.f;
};
}  // namespace batap
