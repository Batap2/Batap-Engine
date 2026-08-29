#pragma once

#include "Renderer/Vulkan/VulkanContext.h"
#include "Renderer/Vulkan/VulkanSwapchain.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

VK_DEFINE_HANDLE(VmaAllocation)

namespace batap
{
struct ResourceManager;
struct ScenePasses;
// Frame model (docs/vulkan.md §10):
//   beginFrame: wait on the slot fence, recycle staging and pending destroys —
//   the frame's uploads are written after that;
//   render: flushUploads → one rendering scope (swapchain + depth) the scene
//   records into (callback set by bindScene) → present.
// Rendering goes straight into the swapchain: no composition pass.
struct Renderer
{
    Renderer(void* nativeWindow, bool transparent = false);
    ~Renderer();

    void render();
    void beginFrame();

    void resize(uint32_t w, uint32_t h);
    void flush();

    using ResizeCallback = std::function<void(uint32_t w, uint32_t h)>;
    void onResize(ResizeCallback cb);

    // Set by bindScene; called inside the rendering scope, every frame.
    ScenePasses* scenePasses();
    using SceneRecordFn = std::function<void(VkCommandBuffer cmd, uint32_t frame, uint32_t width,
                                             uint32_t height)>;
    void setSceneRecord(SceneRecordFn fn);

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
