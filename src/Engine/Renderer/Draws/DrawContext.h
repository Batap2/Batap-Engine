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

inline constexpr VkFormat ViewNormalFormat = VK_FORMAT_A2B10G10R10_UNORM_PACK32;

struct DrawSetup
{
    VulkanContext& ctx_;
    ResourceManager& resources_;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkFormat colorFormat_ = VK_FORMAT_UNDEFINED;
    VkFormat depthFormat_ = VK_FORMAT_UNDEFINED;
    VkPipelineLayout rtLayout_ = VK_NULL_HANDLE;
};

struct DrawContext
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

inline DrawContext bindDraw(const DrawContext& draw, VkCommandBuffer cmd)
{
    DrawContext bound = draw;
    bound.cmd_ = cmd;
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, draw.layout_, 0,
                            static_cast<uint32_t>(draw.sets_.size()), draw.sets_.data(), 0,
                            nullptr);
    if (draw.rtSet_ != VK_NULL_HANDLE)
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, draw.rtLayout_, RayTracingSet,
                                1, &draw.rtSet_, 0, nullptr);
    return bound;
}

inline void pushDraw(const DrawContext& draw, const DrawPush& push)
{
    vkCmdPushConstants(draw.cmd_, draw.layout_,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push),
                       &push);
}
}  // namespace batap
