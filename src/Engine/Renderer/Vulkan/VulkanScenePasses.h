#pragma once

#include <volk.h>

#include "Renderer/SceneBinding.h"
#include "Renderer/Vulkan/Passes/BillboardPass.h"
#include "Renderer/Vulkan/Passes/DebugPass.h"
#include "Renderer/Vulkan/Passes/GeometryPass.h"
#include "Renderer/Vulkan/Passes/ShaderCatalog.h"
#include "Renderer/Vulkan/Passes/SkyPass.h"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace batap
{
struct VulkanContext;
struct ResourceManager;
struct DebugDraw;
struct Billboards;

struct ScenePasses
{
    ScenePasses(VulkanContext& ctx, ResourceManager& resources, VkFormat colorFormat,
                VkFormat depthFormat);
    ~ScenePasses();

    ScenePasses(const ScenePasses&) = delete;
    ScenePasses& operator=(const ScenePasses&) = delete;

    void record(VkCommandBuffer cmd, uint32_t frame, uint32_t width, uint32_t height,
                const SceneRenderArgs& args, Engine& ctx);

    void uploadDebugDraw(const DebugDraw& depthTested, const DebugDraw& overlay);
    void uploadBillboards(const Billboards& billboards);

    void checkHotReload();

   private:
    void writeFrameSet(uint32_t frame, const SceneRenderArgs& args, Engine& ctx);
    void buildPipelines(const ShaderModules& modules);

    VulkanContext& ctx_;
    ResourceManager& resources_;

    VkDescriptorSetLayout frameSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool framePool_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> frameSets_;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;

    GeometryPass geometry_;
    BillboardPass billboards_;
    SkyPass sky_;
    DebugPass debug_;

    std::filesystem::file_time_type shadersMtime_{};
};
}  // namespace batap
