#pragma once

#include "Renderer/Device/ShaderLibrary.h"
#include "Renderer/Draws/DrawContext.h"
#include "Renderer/Graph/RenderGraph.h"

#include <functional>
#include <optional>

namespace batap
{
struct Camera_C;

struct Reflections
{
    struct Images
    {
        GraphImage surface_;
        GraphImage raw_;
        GraphImage ray_;
        GraphImage output_;
    };

    explicit Reflections(const DrawSetup& setup);
    ~Reflections();

    Reflections(const Reflections&) = delete;
    Reflections& operator=(const Reflections&) = delete;

    void buildPipelines(const ShaderModules& modules);
    std::optional<Images> createImages(RenderGraph& graph, VkExtent2D extent,
                                       const Camera_C& camera, bool rayTracing);
    void addPasses(RenderGraph& graph, GraphImage depth, GraphImage normals,
                   const DrawContext& draw, const std::function<void(GraphPass&)>& readsShading);

   private:
    DrawSetup setup_;
    VkPipeline trace_ = VK_NULL_HANDLE;
    std::optional<Images> images_;
};
}  // namespace batap
