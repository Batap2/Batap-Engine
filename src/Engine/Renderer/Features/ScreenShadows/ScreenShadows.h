#pragma once

#include "Renderer/Device/ShaderLibrary.h"
#include "Renderer/Draws/DrawContext.h"
#include "Renderer/Graph/RenderGraph.h"

#include "entt/entt.hpp"

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
    std::optional<GraphImage> createImage(RenderGraph& graph, VkExtent2D extent,
                                          const Camera_C& settings, bool enabled);
    void addPass(RenderGraph& graph, GraphImage depth, GraphBuffer frameConstants,
                 GraphBuffer uploads, const DrawContext& draw);

   private:
    DrawSetup setup_;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    std::optional<GraphImage> image_;
};
}  // namespace batap
