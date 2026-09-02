#pragma once

#include <volk.h>

#include "Renderer/Vulkan/VulkanShaderCompiler.h"

#include <cstdint>

VK_DEFINE_HANDLE(VmaAllocator)

namespace batap
{
struct VulkanContext
{
    VulkanContext();
    ~VulkanContext();

    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;

    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    uint32_t graphicsQueueFamily_ = 0;

    ShaderCompiler shaderCompiler_;

    VmaAllocator allocator_ = nullptr;

    uint32_t frameIndex_ = 0;
};
}  // namespace batap
