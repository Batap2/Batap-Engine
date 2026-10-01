#include "VulkanRenderer.h"

#include "VulkanBarrier.h"
#include "VulkanMemory.h"
#include "VulkanResources.h"
#include "VulkanScenePasses.h"

#include "Paths.h"
#include "Platform/PlatformWindow.h"
#include "Renderer/EngineConfig.h"

#include <backends/imgui_impl_vulkan.h>
#include <imgui.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace batap
{

namespace
{
constexpr VkFormat DepthFormat = VK_FORMAT_D32_SFLOAT;
}

Renderer::Renderer(void* nativeWindow, bool transparent)
    : swapchain_(ctx_, platformSurfaceHandle(nativeWindow), transparent)
{
    transparent_ = transparent;

    // Render size is the swapchain extent, in physical pixels — the requested
    // client size is in points on a retina screen.
    width_ = swapchain_.extent_.width;
    height_ = swapchain_.extent_.height;
    viewExtent_ = swapchain_.extent_;
    createDepthBuffer();

    resources_ = std::make_unique<ResourceManager>(ctx_);
    resourceManager_ = resources_.get();

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = ctx_.graphicsQueueFamily_;
    if (vkCreateCommandPool(ctx_.device_, &poolInfo, nullptr, &commandPool_) != VK_SUCCESS)
        throw std::runtime_error("Renderer(vk) : command pool");

    commandBuffers_.resize(FramesInFlight);
    VkCommandBufferAllocateInfo cmdInfo{};
    cmdInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdInfo.commandPool = commandPool_;
    cmdInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdInfo.commandBufferCount = FramesInFlight;
    if (vkAllocateCommandBuffers(ctx_.device_, &cmdInfo, commandBuffers_.data()) != VK_SUCCESS)
        throw std::runtime_error("Renderer(vk) : command buffers");

#if defined(_WIN32)
    char* dumpEnv = nullptr;
    size_t dumpEnvLen = 0;
    _dupenv_s(&dumpEnv, &dumpEnvLen, "BATAP_DUMP_FRAME");
    if (dumpEnv)
        dumpAtFrame_ = std::atoll(dumpEnv);
    free(dumpEnv);
#else
    if (const char* dumpEnv = std::getenv("BATAP_DUMP_FRAME"))
        dumpAtFrame_ = std::atoll(dumpEnv);
#endif

    window_ = nativeWindow;
    initImGui();

    std::cout << "[Vulkan] Renderer ready — swapchain " << swapchain_.extent_.width << "x"
              << swapchain_.extent_.height << std::endl;
}

// The swapchain image must already be in TRANSFER_SRC when this runs.
void Renderer::recordDumpCopy(VkCommandBuffer cmd, uint32_t imageIndex)
{
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = uint64_t(swapchain_.extent_.width) * swapchain_.extent_.height * 4;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
                      VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo mapped{};
    if (vmaCreateBuffer(ctx_.allocator_, &bufferInfo, &allocInfo, &dumpBuffer_, &dumpAllocation_,
                        &mapped) != VK_SUCCESS)
        return;
    dumpMapped_ = mapped.pMappedData;

    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {swapchain_.extent_.width, swapchain_.extent_.height, 1};
    vkCmdCopyImageToBuffer(cmd, swapchain_.images_[imageIndex],
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dumpBuffer_, 1, &region);
}

void Renderer::writeDump()
{
    vkDeviceWaitIdle(ctx_.device_);
    vmaInvalidateAllocation(ctx_.allocator_, dumpAllocation_, 0, VK_WHOLE_SIZE);

    auto* px = static_cast<uint8_t*>(dumpMapped_);
    const uint64_t count = uint64_t(swapchain_.extent_.width) * swapchain_.extent_.height;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage"
    for (uint64_t i = 0; i < count; ++i)  // swapchain BGRA -> RGBA
        std::swap(px[i * 4 + 0], px[i * 4 + 2]);
#pragma clang diagnostic pop
    stbi_write_png("frame_dump.png", int(swapchain_.extent_.width), int(swapchain_.extent_.height),
                   4, px, int(swapchain_.extent_.width * 4));
    std::cout << "[Vulkan] frame_dump.png écrite (frame " << frameCounter_ << ")" << std::endl;

    vmaDestroyBuffer(ctx_.allocator_, dumpBuffer_, dumpAllocation_);
    dumpBuffer_ = VK_NULL_HANDLE;
    dumpAllocation_ = nullptr;
    dumpMapped_ = nullptr;
}

void Renderer::createDepthBuffer()
{
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = DepthFormat;
    imageInfo.extent = {viewExtent_.width, viewExtent_.height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    if (vmaCreateImage(ctx_.allocator_, &imageInfo, &allocInfo, &depthImage_, &depthAllocation_,
                       nullptr) != VK_SUCCESS)
        throw std::runtime_error("Renderer(vk) : depth image");

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = depthImage_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = DepthFormat;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(ctx_.device_, &viewInfo, nullptr, &depthView_) != VK_SUCCESS)
        throw std::runtime_error("Renderer(vk) : depth view");
}

// Made on first use: only a host that narrows or shows the view ever needs it.
void Renderer::ensureSceneImage()
{
    if (sceneImage_ != VK_NULL_HANDLE)
        return;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = swapchain_.format_;
    imageInfo.extent = {viewExtent_.width, viewExtent_.height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                      VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    if (vmaCreateImage(ctx_.allocator_, &imageInfo, &allocInfo, &sceneImage_, &sceneAllocation_,
                       nullptr) != VK_SUCCESS)
        throw std::runtime_error("Renderer(vk) : scene image");

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = sceneImage_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = swapchain_.format_;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(ctx_.device_, &viewInfo, nullptr, &sceneView_) != VK_SUCCESS)
        throw std::runtime_error("Renderer(vk) : scene view");
    sceneLastUsage_ = Usage::None;
}

void Renderer::retireTargets()
{
    auto& slot = retired_[ctx_.frameIndex_];
    if (depthImage_ != VK_NULL_HANDLE)
        slot.push_back({depthImage_, depthAllocation_, depthView_, VK_NULL_HANDLE});
    depthImage_ = VK_NULL_HANDLE;
    depthAllocation_ = nullptr;
    depthView_ = VK_NULL_HANDLE;

    if (sceneImage_ != VK_NULL_HANDLE)
        slot.push_back({sceneImage_, sceneAllocation_, sceneView_, sceneTextureSet_});
    sceneImage_ = VK_NULL_HANDLE;
    sceneAllocation_ = nullptr;
    sceneView_ = VK_NULL_HANDLE;
    sceneTextureSet_ = VK_NULL_HANDLE;
    sceneLastUsage_ = Usage::None;
}

void Renderer::destroyRetired(uint32_t slot)
{
    for (const Retired& r : retired_[slot])
    {
        if (r.imguiSet_ != VK_NULL_HANDLE)
            ImGui_ImplVulkan_RemoveTexture(r.imguiSet_);
        vkDestroyImageView(ctx_.device_, r.view_, nullptr);
        vmaDestroyImage(ctx_.allocator_, r.image_, r.allocation_);
    }
    retired_[slot].clear();
}

void Renderer::destroyAllRetired()
{
    for (uint32_t slot = 0; slot < FramesInFlight; ++slot)
        destroyRetired(slot);
}

void Renderer::initImGui()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    const std::string robotoPath =
        resolveEngineFile("assets/Roboto-Medium.ttf", "assets/Roboto-Medium.ttf");
    io.Fonts->AddFontFromFileTTF(robotoPath.c_str(), 15.0f);

    ImFontConfig cfg;
    cfg.MergeMode = true;
    cfg.GlyphOffset = ImVec2(0.0f, Renderer::kIconGlyphOffsetY);
    const std::string iconsPath =
        resolveEngineFile("assets/MaterialIcons-Regular.ttf", "assets/MaterialIcons-Regular.ttf");
    io.Fonts->AddFontFromFileTTF(iconsPath.c_str(), 14.0f, &cfg);

    smallFont_ = io.Fonts->AddFontFromFileTTF(robotoPath.c_str(), 14.0f);
    ImFontConfig smallIcons = cfg;
    smallIcons.GlyphOffset = ImVec2(0.0f, 1.0f);
    io.Fonts->AddFontFromFileTTF(iconsPath.c_str(), 12.0f, &smallIcons);

    const std::string monoPath =
        resolveEngineFile("assets/Cousine-Regular.ttf", "assets/Cousine-Regular.ttf");
    monoFont_ = io.Fonts->AddFontFromFileTTF(monoPath.c_str(), 14.0f);

    ImGui::StyleColorsDark();

    platformImGuiInit(window_);

    // The backend draws inside the frame's rendering scope: its pipeline must
    // declare the same attachments (swapchain color + depth).
    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion = VK_API_VERSION_1_3;
    info.Instance = ctx_.instance_;
    info.PhysicalDevice = ctx_.physicalDevice_;
    info.Device = ctx_.device_;
    info.QueueFamily = ctx_.graphicsQueueFamily_;
    info.Queue = ctx_.graphicsQueue_;
    info.DescriptorPoolSize = 64;
    info.MinImageCount = 2;
    info.ImageCount = static_cast<uint32_t>(swapchain_.images_.size());
    info.UseDynamicRendering = true;

    VkPipelineRenderingCreateInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &swapchain_.format_;  // deep-copied by the backend
    rendering.depthAttachmentFormat = DepthFormat;
    info.PipelineInfoMain.PipelineRenderingCreateInfo = rendering;

    if (!ImGui_ImplVulkan_Init(&info))
        throw std::runtime_error("Renderer(vk) : ImGui_ImplVulkan_Init");
}

ScenePasses* Renderer::scenePasses()
{
    if (!scenePasses_)
    {
        scenePasses_ =
            std::make_unique<ScenePasses>(ctx_, *resources_, swapchain_.format_, DepthFormat);
    }
    return scenePasses_.get();
}

void Renderer::uploadDebugDraw(const DebugDraw& depthTested, const DebugDraw& overlay)
{
    if (scenePasses_)
        scenePasses_->uploadDebugDraw(depthTested, overlay);
}

void Renderer::uploadBillboards(const Billboards& billboards)
{
    if (scenePasses_)
        scenePasses_->uploadBillboards(billboards);
}

void Renderer::setSceneRecord(SceneRecordFn fn)
{
    sceneRecord_ = std::move(fn);
}

void Renderer::beginFrame()
{
    swapchain_.waitFrame();
    destroyRetired(ctx_.frameIndex_);
    resources_->beginFrame();

    if (!imguiFrameOpen_)
    {
        ImGui_ImplVulkan_NewFrame();
        platformImGuiNewFrame(window_);
        ImGui::NewFrame();
        imguiFrameOpen_ = true;
    }
}

void Renderer::render()
{
    const uint32_t imageIndex = swapchain_.acquire();
    if (imageIndex == VulkanSwapchain::OutOfDate)
    {
        resize(0, 0);
        return;
    }
    const uint32_t frame = ctx_.frameIndex_;

    // Shader hot reload, outside command recording: a rebuild waits for GPU idle.
    if (scenePasses_ && frameCounter_ % 30 == 0)
        scenePasses_->checkHotReload();

    VkCommandBuffer cmd = commandBuffers_[frame];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    resources_->flushUploads(cmd);

    const bool shownByUi = sceneShownByUi_;
    sceneShownByUi_ = false;
    const bool fullView = viewExtent_.width == swapchain_.extent_.width &&
                          viewExtent_.height == swapchain_.extent_.height;

    VkImage swapImage = swapchain_.images_[imageIndex];
    RenderTargets targets{};
    targets.depth_ = depthView_;
    targets.clearColor_ = {{0.0f, 0.0f, 0.0f, transparent_ ? 0.0f : 1.0f}};

    // Discard already drops the contents, so `from` here only says what must
    // finish before the transition — hence a real usage rather than None:
    //   swapchain: None means TOP_OF_PIPE, earlier than the stage where the
    //     acquire semaphore is waited (COLOR_ATTACHMENT_OUTPUT), so the
    //     transition could overwrite an image the compositor is still reading;
    //   scene image: the ImGui pass that read it last frame;
    //   depth: one image for all frames in flight — wait on the depth tests of
    //     the previous frame before reusing it.
    bool swapchainHasScene = false;
    if (!shownByUi && fullView)
    {
        // Nobody narrows or shows the view: straight into the swapchain. The
        // game never pays for the editor's scene image, not even its memory.
        BarrierBatch{}
            .image(swapImage, Usage::ColorAttachment, Usage::ColorAttachment, colorRange(),
                   Discard::Yes)
            .image(depthImage_, Usage::DepthAttachment, Usage::DepthAttachment, depthRange(),
                   Discard::Yes)
            .flush(cmd);

        targets.color_ = swapchain_.views_[imageIndex];
        targets.extent_ = swapchain_.extent_;
        swapchainHasScene = sceneRecord_ && sceneRecord_(cmd, frame, targets);
    }
    else
    {
        // The host shows the image itself; a host that narrows the view
        // without showing it simply sees no scene. The swapchain only gets
        // the UI.
        ensureSceneImage();
        const Usage sceneFrom = sceneLastUsage_ == Usage::None ? Usage::ColorAttachment
                                                               : sceneLastUsage_;
        BarrierBatch{}
            .image(sceneImage_, sceneFrom, Usage::ColorAttachment, colorRange(), Discard::Yes)
            .image(depthImage_, Usage::DepthAttachment, Usage::DepthAttachment, depthRange(),
                   Discard::Yes)
            .image(swapImage, Usage::ColorAttachment, Usage::ColorAttachment, colorRange(),
                   Discard::Yes)
            .flush(cmd);

        targets.color_ = sceneView_;
        targets.extent_ = viewExtent_;
        if (sceneRecord_ && sceneRecord_(cmd, frame, targets))
        {
            BarrierBatch{}
                .image(sceneImage_, Usage::ColorAttachment, Usage::ShaderRead, colorRange())
                .flush(cmd);
        }
        else
        {
            // No camera: the image is shown anyway, so it gets the clear color.
            BarrierBatch{}
                .image(sceneImage_, Usage::ColorAttachment, Usage::TransferDst, colorRange(),
                       Discard::Yes)
                .flush(cmd);
            const VkImageSubresourceRange range = colorRange();
            vkCmdClearColorImage(cmd, sceneImage_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                 &targets.clearColor_, 1, &range);
            BarrierBatch{}
                .image(sceneImage_, Usage::TransferDst, Usage::ShaderRead, colorRange())
                .flush(cmd);
        }
        sceneLastUsage_ = Usage::ShaderRead;
    }

    if (imguiFrameOpen_ || !swapchainHasScene)
    {
        VkRenderingAttachmentInfo uiColor{};
        uiColor.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        uiColor.imageView = swapchain_.views_[imageIndex];
        uiColor.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        uiColor.loadOp = swapchainHasScene ? VK_ATTACHMENT_LOAD_OP_LOAD
                                           : VK_ATTACHMENT_LOAD_OP_CLEAR;
        uiColor.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        uiColor.clearValue.color = targets.clearColor_;

        VkRenderingInfo uiInfo{};
        uiInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        uiInfo.renderArea = {{0, 0}, swapchain_.extent_};
        uiInfo.layerCount = 1;
        uiInfo.colorAttachmentCount = 1;
        uiInfo.pColorAttachments = &uiColor;

        vkCmdBeginRendering(cmd, &uiInfo);
        if (imguiFrameOpen_)
        {
            ImGui::Render();
            ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
            imguiFrameOpen_ = false;
        }
        vkCmdEndRendering(cmd);
    }

    auto swapchainBarrier = [&](Usage from, Usage to)
    { BarrierBatch{}.image(swapchain_.images_[imageIndex], from, to, colorRange()).flush(cmd); };

    const bool dumpThisFrame = dumpAtFrame_ >= 0 &&
                               frameCounter_ == static_cast<uint64_t>(dumpAtFrame_);
    if (dumpThisFrame)
    {
        swapchainBarrier(Usage::ColorAttachment, Usage::TransferSrc);
        recordDumpCopy(cmd, imageIndex);
        swapchainBarrier(Usage::TransferSrc, Usage::Present);
    }
    else
    {
        swapchainBarrier(Usage::ColorAttachment, Usage::Present);
    }

    vkEndCommandBuffer(cmd);
    swapchain_.submit(cmd, ctx_.graphicsQueue_);
    swapchain_.present(ctx_.graphicsQueue_, imageIndex);

    if (dumpThisFrame && dumpBuffer_)
        writeDump();
    ++frameCounter_;
}

void Renderer::resize(uint32_t w, uint32_t h)
{
    if (w == width_ && h == height_)
        return;

    swapchain_.recreate();  // waits for GPU idle, follows the surface size

    retireTargets();
    width_ = swapchain_.extent_.width;
    height_ = swapchain_.extent_.height;
    viewExtent_ = swapchain_.extent_;
    createDepthBuffer();

    for (auto& cb : resizeCallbacks_)
        cb(width_, height_);
}

void Renderer::setViewExtent(uint32_t w, uint32_t h)
{
    w = std::clamp(w, 1u, swapchain_.extent_.width);
    h = std::clamp(h, 1u, swapchain_.extent_.height);
    if (w == viewExtent_.width && h == viewExtent_.height)
        return;
    // The scene image comes back on its next use, at the new size.
    retireTargets();
    viewExtent_ = {w, h};
    createDepthBuffer();
    for (auto& cb : resizeCallbacks_)
        cb(w, h);
}

ImTextureID Renderer::sceneTexture()
{
    ensureSceneImage();
    if (sceneTextureSet_ == VK_NULL_HANDLE)
        sceneTextureSet_ = ImGui_ImplVulkan_AddTexture(resourceManager_->textureSampler(),
                                                       sceneView_,
                                                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    sceneShownByUi_ = true;
    return reinterpret_cast<ImTextureID>(sceneTextureSet_);
}

void Renderer::setVsync(bool on)
{
    swapchain_.setVsync(on);
}

void Renderer::onResize(ResizeCallback cb)
{
    resizeCallbacks_.push_back(std::move(cb));
}

void Renderer::flush()
{
    vkDeviceWaitIdle(ctx_.device_);
}

ImTextureID Renderer::imguiTexture(GPUResourceHandle image)
{
    if (auto it = imguiTextures_.find(image); it != imguiTextures_.end())
        return it->second;

    VkImageView view = resourceManager_->viewFor(image);
    if (view == VK_NULL_HANDLE)
        return 0;

    VkDescriptorSet set = ImGui_ImplVulkan_AddTexture(resourceManager_->textureSampler(), view,
                                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    const auto id = reinterpret_cast<ImTextureID>(set);
    imguiTextures_.emplace(image, id);
    return id;
}

Renderer::~Renderer()
{
    vkDeviceWaitIdle(ctx_.device_);
    retireTargets();
    destroyAllRetired();
    ImGui_ImplVulkan_Shutdown();
    platformImGuiShutdown();
    ImGui::DestroyContext();
    vkDestroyCommandPool(ctx_.device_, commandPool_, nullptr);
}

}  // namespace batap
