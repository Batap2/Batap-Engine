#pragma once

#include "Handles.h"
#include "Renderer/DebugDraw.h"
#include "Renderer/Device/ShaderLibrary.h"
#include "Renderer/Draws/DebugShapeMesh.h"
#include "Renderer/Draws/DrawContext.h"

#include <array>

namespace batap
{
struct DebugDrawer
{
    explicit DebugDrawer(const DrawSetup& setup);
    ~DebugDrawer();

    DebugDrawer(const DebugDrawer&) = delete;
    DebugDrawer& operator=(const DebugDrawer&) = delete;

    void buildPipelines(const ShaderModules& modules);
    void record(const DrawContext& draw);

    // Staging is filled during the update and copied by flushUploads at the
    // top of the next render — same contract as the instance pools.
    void upload(const DebugDraw& depthTested, const DebugDraw& overlay);

    GPUResourceHandle vertsBuffer() const { return vertsBuffer_; }
    GPUResourceHandle shapesBuffer() const { return shapesBuffer_; }

   private:
    struct Range
    {
        uint32_t firstInstance_ = 0;
        uint32_t instanceCount_ = 0;
    };

    // Depth tested, then drawn over everything.
    static constexpr size_t LayerCount = 2;

    struct Layer
    {
        std::array<Range, DebugDraw::ShapeCount> shapes_{};
        VkPipeline pipeline_ = VK_NULL_HANDLE;
    };

    DrawSetup setup_;
    std::array<Layer, LayerCount> layers_{};
    GPUResourceHandle vertsBuffer_;
    GPUResourceHandle shapesBuffer_;
    std::array<DebugShapeSlice, DebugDraw::ShapeCount> slices_{};
};
}  // namespace batap
