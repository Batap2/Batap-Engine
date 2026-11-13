#pragma once

#include "Renderer/Device/ShaderLibrary.h"
#include "Renderer/Draws/DrawContext.h"
#include "Renderer/Graph/RenderGraph.h"
#include "Shaders/ShaderInterop.h"

#include "entt/entt.hpp"

#include <array>
#include <cstdint>
#include <optional>

namespace batap
{
struct Camera_C;
struct GPUInstanceManager;

struct ScreenShadows
{
    explicit ScreenShadows(const DrawSetup& setup);
    ~ScreenShadows();

    ScreenShadows(const ScreenShadows&) = delete;
    ScreenShadows& operator=(const ScreenShadows&) = delete;

    static uint32_t lightIndex(entt::registry& reg, GPUInstanceManager& instances);

    void buildPipelines(const ShaderModules& modules);
    bool plan(const entt::registry& reg, entt::entity camera, VkExtent2D extent, float aspect,
              FrameGPUData& frame);
    std::optional<GraphImage> createImage(RenderGraph& graph, VkExtent2D extent,
                                          const Camera_C& settings, bool enabled);
    void addPass(RenderGraph& graph, GraphImage depth, GraphBuffer frameConstants,
                 GraphBuffer uploads, const DrawContext& draw);

   private:
    DrawSetup setup_;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    std::optional<GraphImage> image_;
    std::array<std::array<uint32_t, 3>, ScreenShadowDispatches> dispatches_{};
    uint32_t dispatchCount_ = 0;
};
}  // namespace batap
