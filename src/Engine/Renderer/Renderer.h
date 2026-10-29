#pragma once

#include "Handles.h"
#include "Renderer/Device/VulkanContext.h"
#include "Renderer/Device/VulkanSwapchain.h"
#include "Renderer/EngineConfig.h"
#include "Renderer/Graph/RenderGraph.h"

#include <imgui.h>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

VK_DEFINE_HANDLE(VmaAllocation)

namespace batap
{
struct ResourceManager;
struct SceneFrame;
struct RenderTargets;
struct DebugDraw;
struct Billboards;
struct TerrainDraws;
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

    // The scene image, the size of the view, for a host that shows it through
    // ImGui::Image. Asked for this frame, the scene is not drawn into the
    // swapchain: the host is drawing it.
    ImTextureID sceneTexture();

    // The size every render target has: the window unless the host narrows
    // it. A change recreates the targets and fires the resize callbacks.
    void setViewExtent(uint32_t w, uint32_t h);
    VkExtent2D viewExtent() const { return viewExtent_; }

    void render();
    void beginFrame();

    void resize(uint32_t w, uint32_t h);
    void setVsync(bool on);
    void flush();

    using ResizeCallback = std::function<void(uint32_t w, uint32_t h)>;
    void onResize(ResizeCallback cb);

    SceneFrame* sceneFrame();

    using SceneGraphFn = std::function<bool(RenderGraph& graph, uint32_t frame,
                                            const RenderTargets& targets, GraphBuffer uploads)>;
    void setSceneGraph(SceneGraphFn fn);

    void uploadDebugDraw(const DebugDraw& depthTested, const DebugDraw& overlay);
    void uploadBillboards(const Billboards& billboards);
    void uploadTerrain(TerrainDraws& draws, bool rayTracing);

    ResourceManager* resourceManager_ = nullptr;

    uint32_t frameIndex() const { return ctx_.frameIndex_; }

    VulkanContext ctx_;
    VulkanSwapchain swapchain_;

   private:
    void ensureSceneImage();
    bool addScene(uint32_t frame, GraphBuffer uploads, GraphImage output, VkExtent2D extent,
                  VkClearColorValue clearColor);
    void retireTargets();
    void removeRetiredTextureSets(uint32_t slot);

    std::unique_ptr<ResourceManager> resources_;
    std::unique_ptr<SceneFrame> sceneFrame_;
    SceneGraphFn sceneGraph_;
    RenderGraph graph_;

    // Every target is the size of the view: the window unless a host narrows it.
    VkExtent2D viewExtent_{};

    // In the swapchain's format. Null until a host narrows or shows the view:
    // the game renders straight into the swapchain and never allocates it.
    GPUResourceHandle sceneImage_;
    VkDescriptorSet sceneTextureSet_ = VK_NULL_HANDLE;
    bool sceneShownByUi_ = false;
    std::array<std::vector<VkDescriptorSet>, FramesInFlight> retiredTextureSets_{};

    struct SubmitQueue
    {
        VkQueue queue_ = VK_NULL_HANDLE;
        uint32_t pool_ = 0;
    };
    struct CommandPool
    {
        VkCommandPool pool_ = VK_NULL_HANDLE;
        std::vector<VkCommandBuffer> buffers_;
        uint32_t used_ = 0;
    };
    VkCommandBuffer beginCommandBuffer(uint32_t frame, uint32_t pool);
    void submitBatches(uint32_t frame, uint32_t imageIndex, GraphImage swapchain);
    std::vector<SubmitQueue> submitQueues_;
    std::array<std::vector<CommandPool>, FramesInFlight> commandPools_{};

    std::vector<ResizeCallback> resizeCallbacks_;
    bool transparent_ = false;
    ImFont* smallFont_ = nullptr;
    ImFont* monoFont_ = nullptr;
    std::unordered_map<GPUResourceHandle, ImTextureID> imguiTextures_;

    void initImGui();
    void* window_ = nullptr;
    bool imguiFrameOpen_ = false;

    // Debug: BATAP_DUMP_FRAME=N reads frame N back into frame_dump.png
    void recordDumpCopy(VkCommandBuffer cmd, VkImage image);
    void writeDump();
    uint64_t frameCounter_ = 0;
    std::vector<uint64_t> dumpFrames_;
    VkBuffer dumpBuffer_ = VK_NULL_HANDLE;
    VmaAllocation dumpAllocation_ = nullptr;
    void* dumpMapped_ = nullptr;

    void beginGpuFrameTime(VkCommandBuffer cmd, uint32_t frame);
    void endGpuFrameTime(VkCommandBuffer cmd, uint32_t frame);
    float gpuFrameMedianMs() const;
    VkQueryPool frameQueries_ = VK_NULL_HANDLE;
    std::array<bool, FramesInFlight> frameQueriesWritten_{};
    std::array<float, 256> gpuFrameMs_{};
    size_t gpuFrameCount_ = 0;
};
}  // namespace batap
