#pragma once

#include <volk.h>

#include "Renderer/SceneBinding.h"
#include "Renderer/Vulkan/LocalShadowAllocator.h"
#include "Renderer/Vulkan/Passes/BillboardPass.h"
#include "Renderer/Vulkan/Passes/DebugPass.h"
#include "Renderer/Vulkan/Passes/GeometryPass.h"
#include "Renderer/Vulkan/Passes/GiUpdatePass.h"
#include "Renderer/Vulkan/Passes/RtDebugPass.h"
#include "Renderer/Vulkan/Passes/ShaderCatalog.h"
#include "Renderer/Vulkan/Passes/ShadowPass.h"
#include "Renderer/Vulkan/Passes/SkyPass.h"
#include "Renderer/Vulkan/Passes/TerrainPass.h"
#include "Renderer/Vulkan/Passes/TonemapPass.h"
#include "Renderer/Vulkan/RtScene.h"
#include "Renderer/Vulkan/VulkanRenderTargets.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace batap
{
struct VulkanContext;
struct ResourceManager;
struct DebugDraw;
struct Billboards;
struct TerrainDraws;

struct ScenePasses
{
    ScenePasses(VulkanContext& ctx, ResourceManager& resources, VkFormat outputFormat,
                VkFormat depthFormat);
    ~ScenePasses();

    ScenePasses(const ScenePasses&) = delete;
    ScenePasses& operator=(const ScenePasses&) = delete;

    // False without a camera: nothing recorded, the caller owns the clear.
    bool record(VkCommandBuffer cmd, uint32_t frame, const RenderTargets& targets,
                const SceneRenderArgs& args, Engine& ctx);

    void uploadDebugDraw(const DebugDraw& depthTested, const DebugDraw& overlay);
    void uploadBillboards(const Billboards& billboards);
    void uploadTerrain(TerrainDraws& draws);

    void checkHotReload();

   private:
    void writeFrameSet(uint32_t frame, const SceneRenderArgs& args, Engine& ctx);
    void fitCascades(entt::registry& reg, GPUInstanceManager& instances, entt::entity camera,
                     v2i frameSize);
    void buildPipelines(const ShaderModules& modules);
    void recordScene(const PassContext& pass);

    VulkanContext& ctx_;
    ResourceManager& resources_;

    VkDescriptorSetLayout frameSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool framePool_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> frameSets_;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    std::optional<RtScene> rtScene_;
    VkPipelineLayout rtPipelineLayout_ = VK_NULL_HANDLE;

    GPUResourceHandle localAtlas_;
    LocalShadowAllocator localShadowsAlloc_;
    std::vector<ShadowView> shadowViews_;
    GPUResourceHandle cascadeAtlas_;
    GPUResourceHandle ltcMat_;
    GPUResourceHandle ltcAmp_;
    GPUResourceHandle frameConstants_;
    GPUResourceHandle hdrTarget_;
    VkExtent2D hdrExtent_{};
    FrameGPUData frameData_{};
    uint32_t giFrame_ = 0;
    std::vector<ShadowView> cascadeViews_;
    // Bit c: cascade c last reported too close; logged on change only.
    uint32_t warnedCascades_ = 0;

    ShadowPass shadow_;
    GeometryPass geometry_;
    TerrainPass terrain_;
    BillboardPass billboards_;
    SkyPass sky_;
    DebugPass debug_;
    RtDebugPass rtDebug_;
    GiUpdatePass giUpdate_;
    TonemapPass tonemap_;

    std::filesystem::file_time_type shadersMtime_{};
};
}  // namespace batap
