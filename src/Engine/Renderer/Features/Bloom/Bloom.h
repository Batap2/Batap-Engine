#pragma once

#include "Renderer/Device/ShaderLibrary.h"
#include "Renderer/Draws/DrawContext.h"
#include "Renderer/Graph/RenderGraph.h"

#include <optional>
#include <vector>

namespace batap
{
struct Camera_C;

struct Bloom
{
    explicit Bloom(const DrawSetup& setup);
    ~Bloom();

    Bloom(const Bloom&) = delete;
    Bloom& operator=(const Bloom&) = delete;

    void buildPipelines(const ShaderModules& modules);
    std::optional<GraphImage> createImages(RenderGraph& graph, VkExtent2D extent,
                                           const Camera_C& settings, bool enabled);
    float intensity() const { return intensity_; }
    void addPasses(RenderGraph& graph, GraphImage hdr, const DrawContext& draw);

   private:
    DrawSetup setup_;
    VkPipeline down_ = VK_NULL_HANDLE;
    VkPipeline up_ = VK_NULL_HANDLE;
    std::vector<GraphImage> levels_;
    float intensity_ = 0.f;
    float radius_ = 1.f;
};
}  // namespace batap
