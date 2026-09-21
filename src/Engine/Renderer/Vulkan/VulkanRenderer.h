#pragma once

#include "Handles.h"
#include "Renderer/Vulkan/VulkanContext.h"
#include "Renderer/Vulkan/VulkanRenderTargets.h"
#include "Renderer/Vulkan/VulkanSwapchain.h"

#include <imgui.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

VK_DEFINE_HANDLE(VmaAllocation)

namespace batap
{
struct ResourceManager;
struct ScenePasses;
struct DebugDraw;
struct Billboards;
// Frame model (docs/vulkan.md §10):
//   beginFrame: wait on the slot fence, recycle staging and pending destroys —
//   the frame's uploads are written after that;
//   render: flushUploads → the scene records its own scopes (callback set by
//   bindScene) → an ImGui scope on the swapchain → present.
// Rendering goes straight into the swapchain: no composition pass.
struct Renderer
{
    Renderer(void* nativeWindow, bool transparent = false);
    ~Renderer();

    // The icon font is merged shifted down by this much so icons sit on the
    // baseline inside text. Code placing an icon by hand takes it back out.
    static constexpr float kIconGlyphOffsetY = 2.0f;

    ImFont* smallFont() const { return smallFont_; }

    ImFont* monoFont() const { return monoFont_; }

    // The descriptor is made once per handle and kept: ImGui never says when
    // it stops using one.
    ImTextureID imguiTexture(GPUResourceHandle image);

    void render();
    void beginFrame();

    void resize(uint32_t w, uint32_t h);
    void setVsync(bool on);
    void flush();

    using ResizeCallback = std::function<void(uint32_t w, uint32_t h)>;
    void onResize(ResizeCallback cb);

    ScenePasses* scenePasses();

    using SceneRecordFn =
        std::function<bool(VkCommandBuffer cmd, uint32_t frame, const RenderTargets& targets)>;
    void setSceneRecord(SceneRecordFn fn);

    void uploadDebugDraw(const DebugDraw& depthTested, const DebugDraw& overlay);
    void uploadBillboards(const Billboards& billboards);

    ResourceManager* resourceManager_ = nullptr;
    uint32_t width_ = 0;
    uint32_t height_ = 0;

    uint32_t frameIndex() const { return ctx_.frameIndex_; }

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
    bool transparent_ = false;
    ImFont* smallFont_ = nullptr;
    ImFont* monoFont_ = nullptr;
    std::unordered_map<GPUResourceHandle, ImTextureID> imguiTextures_;

    void initImGui();
    void* window_ = nullptr;
    bool imguiFrameOpen_ = false;

    // Debug: BATAP_DUMP_FRAME=N reads frame N back into frame_dump.png
    void recordDumpCopy(VkCommandBuffer cmd, uint32_t imageIndex);
    void writeDump();
    uint64_t frameCounter_ = 0;
    int64_t dumpAtFrame_ = -1;
    VkBuffer dumpBuffer_ = VK_NULL_HANDLE;
    VmaAllocation dumpAllocation_ = nullptr;
    void* dumpMapped_ = nullptr;
};
}  // namespace batap
