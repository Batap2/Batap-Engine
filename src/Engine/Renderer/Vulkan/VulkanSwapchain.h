#pragma once

#include <volk.h>

#include <cstdint>
#include <vector>

namespace batap
{
struct VulkanContext;

struct VulkanSwapchain
{
    // Returned by acquire() once the swapchain is out of date (resize): the
    // caller recreates it and skips the frame.
    static constexpr uint32_t OutOfDate = UINT32_MAX;

    // nativeLayer: a CAMetalLayer* on mac, the HWND on Windows.
    // transparent: alpha composition when the driver exposes it, OPAQUE otherwise.
    VulkanSwapchain(VulkanContext& ctx, void* nativeLayer, bool transparent = false);
    ~VulkanSwapchain();

    VulkanSwapchain(const VulkanSwapchain&) = delete;
    VulkanSwapchain& operator=(const VulkanSwapchain&) = delete;

    // Waits for the GPU to go idle. No-op when the surface has a null extent
    // (minimised).
    void recreate();

    // Split out of acquire() so staging and pending destroys can be recycled at
    // the start of the CPU frame, before the uploads are written. Idempotent.
    void waitFrame();
    uint32_t acquire();
    void submit(VkCommandBuffer cmd, VkQueue queue);
    void present(VkQueue queue, uint32_t imageIndex);

    VkFormat format_ = VK_FORMAT_UNDEFINED;
    VkExtent2D extent_{};
    std::vector<VkImage> images_;
    std::vector<VkImageView> views_;

   private:
    bool createSwapchain();
    void destroyImageResources();

    VulkanContext& ctx_;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;

    uint32_t lastAcquired_ = 0;
    bool frameWaited_ = false;
    bool transparent_ = false;

    // Acquire is per frame in flight; render is per image, because present may
    // wait on a semaphore still tied to that image (spec requirement).
    std::vector<VkSemaphore> acquireSemaphores_;
    std::vector<VkSemaphore> renderSemaphores_;
    std::vector<VkFence> frameFences_;
};
}  // namespace batap
