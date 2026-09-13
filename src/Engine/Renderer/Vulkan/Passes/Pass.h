#pragma once

#include <volk.h>

#include "Shaders/ShaderInterop.h"

#include "entt/entt.hpp"

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
};

// A pass records into an already-open rendering: the descriptor sets are bound
// and push_ already carries cameraIndex_ and the pool counts.
struct PassContext
{
    VkCommandBuffer cmd_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    DrawPush push_{};
    entt::registry* reg_ = nullptr;
    GPUInstanceManager* instanceManager_ = nullptr;
    AssetManager* assetManager_ = nullptr;
};

inline void pushDraw(const PassContext& pass, const DrawPush& push)
{
    vkCmdPushConstants(pass.cmd_, pass.layout_,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push),
                       &push);
}
}  // namespace batap
