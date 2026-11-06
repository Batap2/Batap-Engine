#pragma once

#include "Renderer/Device/ShaderLibrary.h"
#include "Renderer/Draws/DrawContext.h"
#include "Renderer/Graph/RenderGraph.h"

#include <optional>

namespace batap
{
struct Camera_C;

struct Ao
{
    explicit Ao(const DrawSetup& setup);
    ~Ao();

    Ao(const Ao&) = delete;
    Ao& operator=(const Ao&) = delete;

    void buildPipelines(const ShaderModules& modules);
    std::optional<GraphImage> createImage(RenderGraph& graph, VkExtent2D extent,
                                          const Camera_C& settings);
    void addPass(RenderGraph& graph, GraphImage depth, GraphImage normals,
                 GraphBuffer frameConstants, GraphBuffer uploads, const DrawContext& draw);

   private:
    DrawSetup setup_;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    std::optional<GraphImage> image_;
};
}  // namespace batap
