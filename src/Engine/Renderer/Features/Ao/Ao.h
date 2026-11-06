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
    struct Images
    {
        GraphImage raw_;
        GraphImage filtered_;
    };

    explicit Ao(const DrawSetup& setup);
    ~Ao();

    Ao(const Ao&) = delete;
    Ao& operator=(const Ao&) = delete;

    void buildPipelines(const ShaderModules& modules);
    std::optional<Images> createImages(RenderGraph& graph, VkExtent2D extent,
                                       const Camera_C& settings);
    void addPasses(RenderGraph& graph, GraphImage depth, GraphImage normals,
                   GraphBuffer frameConstants, GraphBuffer uploads, const DrawContext& draw);

   private:
    DrawSetup setup_;
    VkPipeline trace_ = VK_NULL_HANDLE;
    VkPipeline filter_ = VK_NULL_HANDLE;
    std::optional<Images> images_;
};
}  // namespace batap
