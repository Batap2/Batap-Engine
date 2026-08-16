#pragma once

#include "Renderer/Vulkan/VulkanContext.h"
#include "Renderer/Vulkan/VulkanResources.h"
#include "Renderer/Vulkan/VulkanSwapchain.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace batap
{
struct ScenePasses;

// Backend Vulkan du Renderer — même nom de classe que la version DX12, même
// surface publique consommée par Engine.cpp / World.cpp (render,
// beginFrame, flush, resize, onResize, resourceManager_, width_/height_,
// frameIndex_). Le header Renderer/Renderer.h sélectionne le backend par
// plateforme.
//
// Architecture de frame (docs/vulkan.md §10) :
//   beginFrame (début de frame CPU) : attend la fence du slot, recycle
//   staging/destructions — les uploads de la frame s'écrivent ensuite ;
//   render : flushUploads → un rendering scope (swapchain + depth) dans
//   lequel la scène s'enregistre (callback posé par SceneRenderer) → present.
// Rendu direct dans la swapchain : la « composition » DX12 était un simple
// CopyResource, elle n'existe plus.
struct Renderer
{
    // Définis dans le .cpp : le unique_ptr<ScenePasses> exige le type complet
    Renderer(void* nativeWindow, uint32_t clientWidth, uint32_t clientHeight,
             bool transparent = false);
    ~Renderer();

    void render();

    // Appelé en début de frame par Engine::beginFrame : fence du slot +
    // NewFrame ImGui (imgui_impl_vulkan + backend plateforme).
    void beginFrame();

    void resize(uint32_t w, uint32_t h);
    void flush();

    using ResizeCallback = std::function<void(uint32_t w, uint32_t h)>;
    void onResize(ResizeCallback cb);

    // Branchement de la scène (posé par SceneRenderer::initRenderPasses) :
    // le callback est appelé dans le rendering scope, chaque frame.
    ScenePasses* scenePasses();
    using SceneRecordFn = std::function<void(VkCommandBuffer cmd, uint32_t frame, uint32_t width,
                                             uint32_t height)>;
    void setSceneRecord(SceneRecordFn fn);

    ResourceManager* resourceManager_ = nullptr;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    uint8_t frameIndex_ = 0;

    VulkanContext ctx_;
    VulkanSwapchain swapchain_;

   private:
    void createDepthBuffer();

    std::unique_ptr<ResourceManager> resources_;
    std::unique_ptr<ScenePasses> scenePasses_;
    SceneRecordFn sceneRecord_;

    VkImage depthImage_ = VK_NULL_HANDLE;
    VmaAllocation depthAllocation_ = nullptr;
    VkImageView depthView_ = VK_NULL_HANDLE;

    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers_;

    std::vector<ResizeCallback> resizeCallbacks_;
    bool frameBegun_ = false;
    bool transparent_ = false;  // clear à alpha 0 (cf. WindowDesc::transparent)

    void initImGui();
    void* window_ = nullptr;  // handle natif, pour platformImGuiNewFrame
    // Un NewFrame ImGui peut rester sans Render (frame sautée sur swapchain
    // périmée) : on ne rouvre pas tant qu'il n'est pas consommé.
    bool imguiFrameOpen_ = false;

    // Debug : BATAP_DUMP_FRAME=N dans l'environnement → la frame N est relue
    // et écrite dans frame_dump.png (validation visuelle sans fenêtre)
    void recordDumpCopy(VkCommandBuffer cmd, uint32_t imageIndex);
    void writeDump();
    uint64_t frameCounter_ = 0;
    int64_t dumpAtFrame_ = -1;
    VkBuffer dumpBuffer_ = VK_NULL_HANDLE;
    VmaAllocation dumpAllocation_ = nullptr;
    void* dumpMapped_ = nullptr;
};
}  // namespace batap
