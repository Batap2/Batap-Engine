// windows.h AVANT volk.h : volk saute alors ses propres typedefs Win32
// minimaux, et vulkan_win32.h a les vrais types
#if defined(_WIN32)
  #include <windows.h>
#endif

#include "VulkanSwapchain.h"

#include "VulkanContext.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace batap
{

void VulkanSwapchain::init(VulkanContext& ctx, void* nativeLayer, uint32_t framesInFlight,
                           bool transparent)
{
    ctx_ = &ctx;
    framesInFlight_ = framesInFlight;
    transparent_ = transparent;

#if defined(VK_USE_PLATFORM_METAL_EXT)
    VkMetalSurfaceCreateInfoEXT surfaceInfo{};
    surfaceInfo.sType = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT;
    surfaceInfo.pLayer = static_cast<const CAMetalLayer*>(nativeLayer);
    if (vkCreateMetalSurfaceEXT(ctx.instance_, &surfaceInfo, nullptr, &surface_) != VK_SUCCESS)
        throw std::runtime_error("VulkanSwapchain : vkCreateMetalSurfaceEXT");
#elif defined(VK_USE_PLATFORM_WIN32_KHR)
    VkWin32SurfaceCreateInfoKHR surfaceInfo{};
    surfaceInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    surfaceInfo.hinstance = ::GetModuleHandleW(nullptr);
    surfaceInfo.hwnd = static_cast<HWND>(nativeLayer);  // platformSurfaceHandle = le HWND
    if (vkCreateWin32SurfaceKHR(ctx.instance_, &surfaceInfo, nullptr, &surface_) != VK_SUCCESS)
        throw std::runtime_error("VulkanSwapchain : vkCreateWin32SurfaceKHR");
#else
    (void)nativeLayer;
    throw std::runtime_error("VulkanSwapchain : plateforme sans implémentation de surface");
#endif

    VkBool32 presentSupported = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(ctx.physicalDevice_, ctx.graphicsQueueFamily_, surface_,
                                         &presentSupported);
    if (!presentSupported)
        throw std::runtime_error("VulkanSwapchain : la queue graphics ne sait pas présenter");

    acquireSemaphores_.resize(framesInFlight_);
    frameFences_.resize(framesInFlight_);

    VkSemaphoreCreateInfo semInfo{};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;  // 1re frame : rien à attendre

    for (auto& s : acquireSemaphores_)
        vkCreateSemaphore(ctx.device_, &semInfo, nullptr, &s);
    for (auto& f : frameFences_)
        vkCreateFence(ctx.device_, &fenceInfo, nullptr, &f);

    if (!createSwapchain())
        throw std::runtime_error("VulkanSwapchain : surface de taille nulle à l'init");
}

bool VulkanSwapchain::createSwapchain()
{
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx_->physicalDevice_, surface_, &caps);
    if (caps.currentExtent.width == 0 || caps.currentExtent.height == 0)
        return false;  // minimisée : rien à créer, on garde l'ancienne
    extent_ = caps.currentExtent;

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(ctx_->physicalDevice_, surface_, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(ctx_->physicalDevice_, surface_, &formatCount,
                                         formats.data());

    VkSurfaceFormatKHR chosen = formats[0];
    for (const auto& f : formats)
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            chosen = f;
    format_ = chosen.format;

    uint32_t imageCount = std::max(caps.minImageCount + 1, 3u);
    if (caps.maxImageCount > 0)
        imageCount = std::min(imageCount, caps.maxImageCount);

    // Sans vsync (comme le renderer DX12) : IMMEDIATE si dispo (MoltenVK le
    // supporte), sinon MAILBOX, sinon FIFO — le seul garanti par la spec.
    uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(ctx_->physicalDevice_, surface_, &presentModeCount,
                                              nullptr);
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(ctx_->physicalDevice_, surface_, &presentModeCount,
                                              presentModes.data());

    auto hasMode = [&](VkPresentModeKHR m)
    { return std::find(presentModes.begin(), presentModes.end(), m) != presentModes.end(); };

    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    if (hasMode(VK_PRESENT_MODE_IMMEDIATE_KHR))
        presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
    else if (hasMode(VK_PRESENT_MODE_MAILBOX_KHR))
        presentMode = VK_PRESENT_MODE_MAILBOX_KHR;

    // Transparence : c'est le driver qui décide ce que le compositeur sait
    // blender — on prend ce qu'il expose, sinon fenêtre opaque normale.
    VkCompositeAlphaFlagBitsKHR compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (transparent_)
    {
        if (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR)
            compositeAlpha = VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR;
        else if (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR)
            compositeAlpha = VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
        else
            std::cerr << "[Vulkan] transparence demandée mais non supportée par le driver — "
                         "fenêtre opaque\n";
    }

    const VkSwapchainKHR oldSwapchain = swapchain_;

    VkSwapchainCreateInfoKHR swapchainInfo{};
    swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchainInfo.surface = surface_;
    swapchainInfo.minImageCount = imageCount;
    swapchainInfo.imageFormat = chosen.format;
    swapchainInfo.imageColorSpace = chosen.colorSpace;
    swapchainInfo.imageExtent = extent_;
    swapchainInfo.imageArrayLayers = 1;
    swapchainInfo.imageUsage =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchainInfo.preTransform = caps.currentTransform;
    swapchainInfo.compositeAlpha = compositeAlpha;
    swapchainInfo.presentMode = presentMode;
    swapchainInfo.clipped = VK_TRUE;
    swapchainInfo.oldSwapchain = oldSwapchain;
    if (vkCreateSwapchainKHR(ctx_->device_, &swapchainInfo, nullptr, &swapchain_) != VK_SUCCESS)
        throw std::runtime_error("VulkanSwapchain : vkCreateSwapchainKHR");

    destroyImageResources();
    if (oldSwapchain)
        vkDestroySwapchainKHR(ctx_->device_, oldSwapchain, nullptr);

    uint32_t count = 0;
    vkGetSwapchainImagesKHR(ctx_->device_, swapchain_, &count, nullptr);
    images_.resize(count);
    vkGetSwapchainImagesKHR(ctx_->device_, swapchain_, &count, images_.data());

    views_.resize(count);
    for (uint32_t i = 0; i < count; ++i)
    {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = images_[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format_;
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        if (vkCreateImageView(ctx_->device_, &viewInfo, nullptr, &views_[i]) != VK_SUCCESS)
            throw std::runtime_error("VulkanSwapchain : image view");
    }

    VkSemaphoreCreateInfo semInfo{};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    renderSemaphores_.resize(count);
    for (auto& s : renderSemaphores_)
        vkCreateSemaphore(ctx_->device_, &semInfo, nullptr, &s);

    return true;
}

// Détruit ce qui dépend des images de la swapchain (views + sémaphores de
// rendu) — appelé avant recréation et au shutdown.
void VulkanSwapchain::destroyImageResources()
{
    for (auto s : renderSemaphores_)
        vkDestroySemaphore(ctx_->device_, s, nullptr);
    renderSemaphores_.clear();
    for (auto v : views_)
        vkDestroyImageView(ctx_->device_, v, nullptr);
    views_.clear();
    images_.clear();
}

void VulkanSwapchain::recreate()
{
    vkDeviceWaitIdle(ctx_->device_);
    createSwapchain();
}

void VulkanSwapchain::shutdown()
{
    for (auto s : acquireSemaphores_)
        vkDestroySemaphore(ctx_->device_, s, nullptr);
    for (auto f : frameFences_)
        vkDestroyFence(ctx_->device_, f, nullptr);
    acquireSemaphores_.clear();
    frameFences_.clear();

    destroyImageResources();

    if (swapchain_)
        vkDestroySwapchainKHR(ctx_->device_, swapchain_, nullptr);
    if (surface_)
        vkDestroySurfaceKHR(ctx_->instance_, surface_, nullptr);
    swapchain_ = VK_NULL_HANDLE;
    surface_ = VK_NULL_HANDLE;
}

void VulkanSwapchain::waitFrame()
{
    if (frameWaited_)
        return;
    vkWaitForFences(ctx_->device_, 1, &frameFences_[frameIndex_], VK_TRUE, UINT64_MAX);
    vkResetFences(ctx_->device_, 1, &frameFences_[frameIndex_]);
    frameWaited_ = true;
}

uint32_t VulkanSwapchain::acquire()
{
    waitFrame();

    uint32_t imageIndex = 0;
    const VkResult result =
        vkAcquireNextImageKHR(ctx_->device_, swapchain_, UINT64_MAX,
                              acquireSemaphores_[frameIndex_], VK_NULL_HANDLE, &imageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR)
        return OutOfDate;
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
        throw std::runtime_error("VulkanSwapchain : vkAcquireNextImageKHR");
    lastAcquired_ = imageIndex;
    return imageIndex;
}

void VulkanSwapchain::submit(VkCommandBuffer cmd, VkQueue queue)
{
    // On n'attend l'image acquise qu'au moment d'écrire dedans (pas avant)
    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &acquireSemaphores_[frameIndex_];
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &renderSemaphores_[lastAcquired_];
    if (vkQueueSubmit(queue, 1, &submitInfo, frameFences_[frameIndex_]) != VK_SUCCESS)
        throw std::runtime_error("VulkanSwapchain : vkQueueSubmit");
}

void VulkanSwapchain::present(VkQueue queue, uint32_t imageIndex)
{
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderSemaphores_[imageIndex];
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain_;
    presentInfo.pImageIndices = &imageIndex;
    const VkResult result = vkQueuePresentKHR(queue, &presentInfo);
    // OUT_OF_DATE ici : la frame est perdue mais l'acquire suivant recréera
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR &&
        result != VK_ERROR_OUT_OF_DATE_KHR)
        throw std::runtime_error("VulkanSwapchain : vkQueuePresentKHR");

    frameIndex_ = (frameIndex_ + 1) % framesInFlight_;
    frameWaited_ = false;
}

}  // namespace batap
