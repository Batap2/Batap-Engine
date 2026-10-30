#pragma once

#include "Handles.h"
#include "Renderer/Device/ShaderLibrary.h"
#include "Renderer/Draws/DrawContext.h"
#include "Renderer/EngineConfig.h"

#include <array>
#include <optional>
#include <vector>

namespace batap
{
struct Camera_C;
struct GraphBuffer;
struct GraphImage;
struct RenderGraph;

struct Exposure
{
    explicit Exposure(const DrawSetup& setup);
    ~Exposure();

    Exposure(const Exposure&) = delete;
    Exposure& operator=(const Exposure&) = delete;

    void buildPipelines(const ShaderModules& modules);
    float update(uint32_t frame, entt::entity camera, const Camera_C& settings, float dt);
    void addPasses(RenderGraph& graph, GraphImage hdr, GraphBuffer frameConstants,
                   const DrawContext& draw, uint32_t frame);

   private:
    struct Cell
    {
        float logLuminance_;
        float weight_;
    };

    std::optional<float> meteredLuminance(float percentile, float range);

    DrawSetup setup_;
    VkPipeline measurePipeline_ = VK_NULL_HANDLE;
    std::array<GPUResourceHandle, FramesInFlight> readback_;
    std::array<bool, FramesInFlight> measured_{};
    std::vector<float> sums_;
    std::vector<Cell> cells_;
    entt::entity camera_ = entt::null;
    bool auto_ = false;
    float ev100_ = 0.f;
};
}  // namespace batap
