#pragma once

#include <volk.h>

#include <cstdint>

VK_DEFINE_HANDLE(VmaAllocator)

namespace batap
{
// Fondation du backend Vulkan : instance, device, queues, allocateur.
// Headless pour l'instant — la surface et la swapchain arrivent avec la
// couche fenêtre (Windows). Cible : Vulkan 1.3 core (dynamic rendering,
// synchronization2, timeline semaphores, descriptor indexing).
struct VulkanContext
{
    void init();
    void shutdown();

    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;

    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    uint32_t graphicsQueueFamily_ = 0;

    VmaAllocator allocator_ = nullptr;
};
}  // namespace batap
