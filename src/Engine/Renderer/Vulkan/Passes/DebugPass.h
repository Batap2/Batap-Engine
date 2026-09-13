#pragma once

#include "Handles.h"
#include "Renderer/DebugDraw.h"
#include "Renderer/DebugShapeMesh.h"
#include "Renderer/Vulkan/Passes/Pass.h"
#include "Renderer/Vulkan/Passes/ShaderCatalog.h"

#include <array>

namespace batap
{
struct DebugPass
{
    explicit DebugPass(const PassSetup& setup);
    ~DebugPass();

    DebugPass(const DebugPass&) = delete;
    DebugPass& operator=(const DebugPass&) = delete;

    void buildPipelines(const ShaderModules& modules);
    void record(const PassContext& pass);

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

    PassSetup setup_;
    std::array<Layer, LayerCount> layers_{};
    GPUResourceHandle vertsBuffer_;
    GPUResourceHandle shapesBuffer_;
    std::array<DebugShapeSlice, DebugDraw::ShapeCount> slices_{};
};
}  // namespace batap
