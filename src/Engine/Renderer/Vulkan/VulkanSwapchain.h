#pragma once

#include <volk.h>

#include <cstdint>
#include <vector>

namespace batap
{
struct VulkanContext;

// Surface + swapchain + synchro de présentation.
// L'équivalent de : IDXGISwapChain4 + waitable object + les fences de frame.
//
// nativeLayer : sur mac, un CAMetalLayer* (posé par la couche fenêtre —
// PlatformWindow) ; sur Windows ce sera le HWND via VK_KHR_win32_surface.
//
// Boucle type :
//   uint32_t imageIndex = sc.acquire();          // bloque sur la fence de frame
//   ... enregistrer cmd sur l'image imageIndex ...
//   sc.submit(cmd, queue);                        // wait acquire, signal render
//   sc.present(queue, imageIndex);                // wait render
struct VulkanSwapchain
{
    // acquire() rend cette valeur quand la swapchain est périmée (resize) :
    // l'appelant recrée et saute la frame.
    static constexpr uint32_t OutOfDate = UINT32_MAX;

    // transparent : demande une composition avec alpha (POST/PRE_MULTIPLIED
    // selon ce que le driver expose) ; retombe sur OPAQUE s'il n'offre rien.
    void init(VulkanContext& ctx, void* nativeLayer, uint32_t framesInFlight,
              bool transparent = false);
    void shutdown();

    // Recrée la swapchain à la taille courante de la surface (attend que le
    // GPU soit idle). No-op si la surface a une dimension nulle (minimisée).
    void recreate();

    // Attend (et reset) la fence CPU du slot de frame courant — séparé
    // d'acquire() pour pouvoir recycler staging/destructions en début de
    // frame CPU, avant que les uploads ne soient écrits. Idempotent.
    void waitFrame();
    uint32_t acquire();
    void submit(VkCommandBuffer cmd, VkQueue queue);
    void present(VkQueue queue, uint32_t imageIndex);

    uint32_t frameIndex() const { return frameIndex_; }

    VkFormat format_ = VK_FORMAT_UNDEFINED;
    VkExtent2D extent_{};
    std::vector<VkImage> images_;
    std::vector<VkImageView> views_;

   private:
    // Crée (ou recrée, via oldSwapchain) la swapchain + images/views/sémaphores
    // de rendu à la taille courante de la surface.
    bool createSwapchain();
    void destroyImageResources();

    VulkanContext* ctx_ = nullptr;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;

    uint32_t framesInFlight_ = 0;
    uint32_t frameIndex_ = 0;
    uint32_t lastAcquired_ = 0;
    bool frameWaited_ = false;
    bool transparent_ = false;

    // Par frame en vol : sémaphore d'acquire + fence CPU.
    // Par image de swapchain : sémaphore de fin de rendu (exigence de la spec :
    // present peut attendre un sémaphore encore associé à l'image).
    std::vector<VkSemaphore> acquireSemaphores_;
    std::vector<VkSemaphore> renderSemaphores_;
    std::vector<VkFence> frameFences_;
};
}  // namespace batap
