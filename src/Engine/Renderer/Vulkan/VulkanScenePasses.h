#pragma once

#include <volk.h>

#include "Handles.h"
#include "Renderer/Billboards.h"
#include "Renderer/DebugDraw.h"
#include "Renderer/SceneBinding.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace batap
{
struct VulkanContext;
struct ResourceManager;
struct DrawPush;

struct ScenePasses
{
    ScenePasses(VulkanContext& ctx, ResourceManager& resources, VkFormat colorFormat,
                VkFormat depthFormat);
    ~ScenePasses();

    ScenePasses(const ScenePasses&) = delete;
    ScenePasses& operator=(const ScenePasses&) = delete;

    void record(VkCommandBuffer cmd, uint32_t frame, uint32_t width, uint32_t height,
                const SceneRenderArgs& args, Engine& ctx);

    // Staging is filled during the update and copied by flushUploads at the
    // top of the next render — same contract as the instance pools.
    void uploadDebugDraw(const DebugDraw& depthTested, const DebugDraw& overlay);
    void uploadBillboards(const Billboards& billboards);

    void checkHotReload();

   private:
    void writeFrameSet(uint32_t frame, const SceneRenderArgs& args, Engine& ctx);
    void buildPipelines(VkShaderModule vs, VkShaderModule ps, VkShaderModule skyVS,
                        VkShaderModule skyPS, VkShaderModule debugVS, VkShaderModule debugPS,
                        VkShaderModule billboardVS, VkShaderModule billboardPS);
    void buildDebugGeometry();
    void recordDebug(VkCommandBuffer cmd, DrawPush push);

    // Which slice of the shared unit-wireframe buffer one shape occupies.
    struct DebugShapeGeometry
    {
        uint32_t firstVertex_ = 0;
        uint32_t vertexCount_ = 0;
    };

    struct DebugDrawRange
    {
        uint32_t firstInstance_ = 0;
        uint32_t instanceCount_ = 0;
    };

    // Depth tested, then drawn over everything.
    static constexpr size_t DebugLayerCount = 2;

    struct DebugLayer
    {
        std::array<DebugDrawRange, DebugDraw::ShapeCount> shapes_{};
        VkPipeline pipeline_ = VK_NULL_HANDLE;
    };

    VulkanContext& ctx_;
    ResourceManager& resources_;
    VkFormat colorFormat_ = VK_FORMAT_UNDEFINED;
    VkFormat depthFormat_ = VK_FORMAT_UNDEFINED;

    VkDescriptorSetLayout frameSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool framePool_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> frameSets_;

    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline geometryPipeline_ = VK_NULL_HANDLE;
    VkPipeline skyPipeline_ = VK_NULL_HANDLE;
    VkPipeline billboardPipeline_ = VK_NULL_HANDLE;
    std::array<DebugLayer, DebugLayerCount> debugLayers_{};

    GPUResourceHandle billboardBuffer_;
    uint32_t billboardCount_ = 0;

    GPUResourceHandle debugVertsBuffer_;
    GPUResourceHandle debugShapesBuffer_;
    std::array<DebugShapeGeometry, DebugDraw::ShapeCount> debugGeometry_{};

    std::filesystem::file_time_type shadersMtime_{};
};
}  // namespace batap
