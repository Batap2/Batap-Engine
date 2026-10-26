#pragma once

#include <volk.h>

#include "Shaders/ShaderInterop.h"

#include "entt/entt.hpp"

#include <array>

namespace batap
{
struct VulkanContext;
struct ResourceManager;
struct AssetManager;
struct GPUInstanceManager;

struct PassSetup
{
    VulkanContext& ctx_;
    ResourceManager& resources_;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkFormat colorFormat_ = VK_FORMAT_UNDEFINED;
    VkFormat depthFormat_ = VK_FORMAT_UNDEFINED;
    VkPipelineLayout rtLayout_ = VK_NULL_HANDLE;
};

struct PassContext
{
    VkCommandBuffer cmd_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipelineLayout rtLayout_ = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, 2> sets_{};
    VkDescriptorSet rtSet_ = VK_NULL_HANDLE;
    DrawPush push_{};
    entt::registry* reg_ = nullptr;
    GPUInstanceManager* instanceManager_ = nullptr;
    AssetManager* assetManager_ = nullptr;
    bool rayTracing_ = false;
};

inline PassContext bindPass(const PassContext& pass, VkCommandBuffer cmd)
{
    PassContext bound = pass;
    bound.cmd_ = cmd;
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pass.layout_, 0,
                            static_cast<uint32_t>(pass.sets_.size()), pass.sets_.data(), 0,
                            nullptr);
    if (pass.rtSet_ != VK_NULL_HANDLE)
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pass.rtLayout_, RayTracingSet,
                                1, &pass.rtSet_, 0, nullptr);
    return bound;
}

inline void pushDraw(const PassContext& pass, const DrawPush& push)
{
    vkCmdPushConstants(pass.cmd_, pass.layout_,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push),
                       &push);
}
}  // namespace batap
