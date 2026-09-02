#pragma once

#include <volk.h>

#include "Renderer/SceneBinding.h"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace batap
{
struct VulkanContext;
struct ResourceManager;

struct ScenePasses
{
    ScenePasses(VulkanContext& ctx, ResourceManager& resources, VkFormat colorFormat,
                VkFormat depthFormat);
    ~ScenePasses();

    ScenePasses(const ScenePasses&) = delete;
    ScenePasses& operator=(const ScenePasses&) = delete;

    void record(VkCommandBuffer cmd, uint32_t frame, uint32_t width, uint32_t height,
                const SceneRenderArgs& args, Engine& ctx);

    void checkHotReload();

   private:
    void writeFrameSet(uint32_t frame, const SceneRenderArgs& args, Engine& ctx);
    void buildPipelines(VkShaderModule vs, VkShaderModule ps, VkShaderModule skyVS,
                        VkShaderModule skyPS);

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

    std::filesystem::file_time_type shadersMtime_{};
};
}  // namespace batap
