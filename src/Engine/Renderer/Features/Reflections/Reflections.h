#pragma once

#include "Handles.h"
#include "Renderer/Device/ShaderLibrary.h"
#include "Renderer/Draws/DrawContext.h"
#include "Renderer/Graph/RenderGraph.h"

#include "entt/entt.hpp"

#include <array>
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
        GraphImage resolved_;
        GraphImage history_;
        GraphImage historySurface_;
        GraphImage output_;
        GraphImage outputSurface_;
        bool historyValid_ = false;
    };

    explicit Reflections(const DrawSetup& setup);
    ~Reflections();

    Reflections(const Reflections&) = delete;
    Reflections& operator=(const Reflections&) = delete;

    void buildPipelines(const ShaderModules& modules);
    std::optional<Images> createImages(RenderGraph& graph, VkExtent2D extent,
                                       const Camera_C& camera, entt::entity cameraEntity,
                                       bool rayTracing);
    void addPasses(RenderGraph& graph, GraphImage depth, GraphImage normals,
                   GraphBuffer frameConstants, GraphBuffer uploads, const DrawContext& draw,
                   const std::function<void(GraphPass&)>& readsShading);

   private:
    void resizeHistory(RenderGraph& graph, VkExtent2D extent);
    void releaseHistory(RenderGraph* graph);
    GraphImage importHistory(RenderGraph& graph, const char* name, GPUResourceHandle image);

    DrawSetup setup_;
    VkPipeline trace_ = VK_NULL_HANDLE;
    VkPipeline resolve_ = VK_NULL_HANDLE;
    VkPipeline temporal_ = VK_NULL_HANDLE;
    std::optional<Images> images_;
    std::array<GPUResourceHandle, 2> history_{};
    std::array<GPUResourceHandle, 2> historySurface_{};
    VkExtent2D historyExtent_{};
    uint32_t historyWrite_ = 0;
    bool historyValid_ = false;
    entt::entity historyCamera_ = entt::null;
};
}  // namespace batap
