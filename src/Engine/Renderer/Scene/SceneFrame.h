#pragma once

#include <volk.h>

#include "Renderer/Device/ShaderLibrary.h"
#include "Renderer/Draws/BillboardDrawer.h"
#include "Renderer/Draws/DebugDrawer.h"
#include "Renderer/Draws/FullscreenDrawers.h"
#include "Renderer/Draws/MeshDrawer.h"
#include "Renderer/Draws/TerrainDrawer.h"
#include "Renderer/Features/Exposure/Exposure.h"
#include "Renderer/Features/Gi/GiCache.h"
#include "Renderer/Features/Gi/GiUpdate.h"
#include "Renderer/Features/RayTracing/AccelerationStructures.h"
#include "Renderer/Features/Shadows/Shadows.h"
#include "Renderer/Graph/RenderGraph.h"
#include "Renderer/Scene/FrameBindings.h"
#include "Renderer/SceneBinding.h"

#include <cstdint>
#include <optional>

namespace batap
{
struct VulkanContext;
struct ResourceManager;
struct DebugDraw;
struct Billboards;
struct TerrainDraws;

struct RenderTargets
{
    GraphImage color_;
    VkExtent2D extent_{};
    VkClearColorValue clearColor_{};
};

struct SceneFrame
{
    SceneFrame(VulkanContext& ctx, ResourceManager& resources, VkFormat outputFormat,
               VkFormat depthFormat);

    SceneFrame(const SceneFrame&) = delete;
    SceneFrame& operator=(const SceneFrame&) = delete;

    bool addPasses(RenderGraph& graph, uint32_t frame, const RenderTargets& targets,
                   GraphBuffer uploads, const SceneRenderArgs& args, Engine& ctx);

    void uploadDebugDraw(const DebugDraw& depthTested, const DebugDraw& overlay);
    void uploadBillboards(const Billboards& billboards);
    void uploadTerrain(TerrainDraws& draws);

    void checkHotReload();

   private:
    void buildPipelines(const ShaderModules& modules);
    void writeFrameSet(uint32_t frame, const SceneRenderArgs& args, Engine& ctx);
    DrawContext drawContext(uint32_t frame, uint32_t camera, const SceneRenderArgs& args,
                            Engine& ctx, bool rayTracing) const;

    ResourceManager& resources_;
    VkFormat depthFormat_ = VK_FORMAT_UNDEFINED;
    ShaderLibrary shaders_;
    FrameBindings bindings_;
    std::optional<AccelerationStructures> accelerationStructures_;
    std::optional<GiCache> giCache_;

    GPUResourceHandle ltcMat_;
    GPUResourceHandle ltcAmp_;
    GPUResourceHandle frameConstants_;
    FrameGPUData frameData_{};

    Shadows shadows_;
    MeshDrawer meshes_;
    TerrainDrawer terrain_;
    BillboardDrawer billboards_;
    FullscreenDrawers fullscreen_;
    DebugDrawer debug_;
    GiUpdate giUpdate_;
    Exposure exposure_;
};
}  // namespace batap
