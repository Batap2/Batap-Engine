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
#include <string>

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

    VkQueryPoolCreateInfo queryInfo{};
    queryInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    queryInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
    queryInfo.queryCount = 2 * FramesInFlight;
    if (vkCreateQueryPool(ctx_.device_, &queryInfo, nullptr, &frameQueries_) != VK_SUCCESS)
        throw std::runtime_error("Renderer(vk) : frame timestamps");

    const auto parseDumpFrames = [&](const std::string& list)
    {
        size_t begin = 0;
        while (begin <= list.size())
        {
            const size_t comma = list.find(',', begin);
            const std::string item =
                list.substr(begin, comma == std::string::npos ? std::string::npos : comma - begin);
            if (!item.empty())
                dumpFrames_.push_back(std::strtoull(item.c_str(), nullptr, 10));
            if (comma == std::string::npos)
                break;
            begin = comma + 1;
        }
    };
#if defined(_WIN32)
    char* dumpEnv = nullptr;
    size_t dumpEnvLen = 0;
    _dupenv_s(&dumpEnv, &dumpEnvLen, "BATAP_DUMP_FRAME");
    if (dumpEnv)
        parseDumpFrames(std::string(dumpEnv));
    free(dumpEnv);
#else
    if (const char* dumpEnv = std::getenv("BATAP_DUMP_FRAME"))
        parseDumpFrames(std::string(dumpEnv));
#endif

    window_ = nativeWindow;
    initImGui();

    std::cout << "[Vulkan] Renderer ready — swapchain " << swapchain_.extent_.width << "x"
              << swapchain_.extent_.height << std::endl;
}

// The swapchain image must already be in TRANSFER_SRC when this runs.
void Renderer::recordDumpCopy(VkCommandBuffer cmd, VkImage image)
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
    vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dumpBuffer_, 1,
                           &region);
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
    const auto timed = static_cast<std::ptrdiff_t>(std::min(gpuFrameCount_, gpuFrameMs_.size()));
    if (timed > 0)
    {
        const auto [low, high] =
            std::minmax_element(gpuFrameMs_.begin(), gpuFrameMs_.begin() + timed);
        std::cout << "[Vulkan] frame GPU : médiane " << gpuFrameMedianMs() << " ms, min " << *low
                  << " ms, max " << *high << " ms sur " << timed << " frames\n";
    }

    const std::string name = dumpFrames_.size() == 1
                                 ? std::string("frame_dump.png")
                                 : "frame_dump_" + std::to_string(frameCounter_) + ".png";
    stbi_write_png(name.c_str(), int(swapchain_.extent_.width), int(swapchain_.extent_.height), 4,
                   px, int(swapchain_.extent_.width * 4));
    std::cout << "[Vulkan] " << name << " écrite (frame " << frameCounter_ << ")" << std::endl;

    vmaDestroyBuffer(ctx_.allocator_, dumpBuffer_, dumpAllocation_);
    dumpBuffer_ = VK_NULL_HANDLE;
    dumpAllocation_ = nullptr;
    dumpMapped_ = nullptr;
}

void Renderer::beginGpuFrameTime(VkCommandBuffer cmd, uint32_t frame)
{
    const uint32_t first = frame * 2;
    if (frameQueriesWritten_[frame])
    {
        std::array<uint64_t, 2> ticks{};
        if (vkGetQueryPoolResults(ctx_.device_, frameQueries_, first, 2, sizeof ticks, ticks.data(),
                                  sizeof(uint64_t), VK_QUERY_RESULT_64_BIT) == VK_SUCCESS)
            gpuFrameMs_[gpuFrameCount_++ % gpuFrameMs_.size()] =
                static_cast<float>(ticks[1] - ticks[0]) * ctx_.timestampPeriodNs_ * 1e-6f;
    }
    vkCmdResetQueryPool(cmd, frameQueries_, first, 2);
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, frameQueries_, first);
}

void Renderer::endGpuFrameTime(VkCommandBuffer cmd, uint32_t frame)
{
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, frameQueries_, frame * 2 + 1);
    frameQueriesWritten_[frame] = true;
}

float Renderer::gpuFrameMedianMs() const
{
    const auto count = static_cast<std::ptrdiff_t>(std::min(gpuFrameCount_, gpuFrameMs_.size()));
    if (count == 0)
        return 0.f;
    std::vector<float> times(gpuFrameMs_.begin(), gpuFrameMs_.begin() + count);
    const auto middle = times.begin() + count / 2;
    std::nth_element(times.begin(), middle, times.end());
    return *middle;
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
    {
        slot.push_back({sceneImage_, sceneAllocation_, sceneView_, sceneTextureSet_});
        graph_.forget(sceneImage_);
    }
    sceneImage_ = VK_NULL_HANDLE;
    sceneAllocation_ = nullptr;
    sceneView_ = VK_NULL_HANDLE;
    sceneTextureSet_ = VK_NULL_HANDLE;
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

void Renderer::uploadTerrain(TerrainDraws& draws)
{
    if (scenePasses_)
        scenePasses_->uploadTerrain(draws);
}

void Renderer::setSceneGraph(SceneGraphFn fn)
{
    sceneGraph_ = std::move(fn);
}

bool Renderer::addScene(uint32_t frame, GraphBuffer uploads, GraphImage output, VkExtent2D extent,
                        VkClearColorValue clearColor)
{
    if (!sceneGraph_)
        return false;

    GraphImageInfo depth;
    depth.image_ = depthImage_;
    depth.view_ = depthView_;
    depth.extent_ = extent;
    depth.aspect_ = VK_IMAGE_ASPECT_DEPTH_BIT;
    RenderTargets targets;
    targets.color_ = output;
    targets.depth_ = graph_.importImage("depth", depth, Usage::DepthAttachment, Usage::None);
    targets.extent_ = extent;
    targets.clearColor_ = clearColor;
    return sceneGraph_(graph_, frame, targets, uploads);
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
    beginGpuFrameTime(cmd, frame);

    graph_.reset();
    const GraphBuffer uploads =
        graph_.importBuffer("uploads", VK_NULL_HANDLE, Usage::AnyRead, Usage::AnyRead);
    if (resources_->hasUploads())
        graph_.addPass("uploads")
            .write(uploads, Usage::TransferDst)
            .execute([this](GraphContext& c) { resources_->flushUploads(c.cmd()); });

    const bool shownByUi = sceneShownByUi_;
    sceneShownByUi_ = false;
    const bool fullView = viewExtent_.width == swapchain_.extent_.width &&
                          viewExtent_.height == swapchain_.extent_.height;

    const VkClearColorValue clearColor{{0.0f, 0.0f, 0.0f, transparent_ ? 0.0f : 1.0f}};

    //   swapchain: None means TOP_OF_PIPE, earlier than the stage where the
    //     acquire semaphore is waited (COLOR_ATTACHMENT_OUTPUT), so the
    //     transition could overwrite an image the compositor is still reading;
    GraphImageInfo swapInfo;
    swapInfo.image_ = swapchain_.images_[imageIndex];
    swapInfo.view_ = swapchain_.views_[imageIndex];
    swapInfo.extent_ = swapchain_.extent_;
    const GraphImage swapchain =
        graph_.importImage("swapchain", swapInfo, Usage::ColorAttachment, Usage::Present);

    const bool intoSwapchain = !shownByUi && fullView;
    GraphImage sceneImage;
    bool sceneDeclared = false;
    if (intoSwapchain)
    {
        // Nobody narrows or shows the view: straight into the swapchain. The
        // game never pays for the editor's scene image, not even its memory.
        sceneDeclared = addScene(frame, uploads, swapchain, swapchain_.extent_, clearColor);
    }
    else
    {
        // The host shows the image itself; a host that narrows the view
        // without showing it simply sees no scene. The swapchain only gets
        // the UI.
        ensureSceneImage();
        GraphImageInfo sceneInfo;
        sceneInfo.image_ = sceneImage_;
        sceneInfo.view_ = sceneView_;
        sceneInfo.extent_ = viewExtent_;
        sceneImage = graph_.importImage("scene image", sceneInfo, Usage::ShaderRead);
        sceneDeclared = addScene(frame, uploads, sceneImage, viewExtent_, clearColor);
        // No camera: the image is shown anyway, so it gets the clear color.
        if (!sceneDeclared)
            graph_.addPass("scene image clear").color(sceneImage, Load::Clear, clearColor);
    }

    const bool swapchainHasScene = intoSwapchain && sceneDeclared;
    const bool drawUi = imguiFrameOpen_;
    if (drawUi)
    {
        ImGui::Render();
        imguiFrameOpen_ = false;
    }
    if (drawUi || !swapchainHasScene)
    {
        GraphPass& ui =
            graph_.addPass("imgui")
                .color(swapchain, swapchainHasScene ? Load::Keep : Load::Clear, clearColor)
                .read(uploads, Usage::AnyRead)
                .execute(
                    [drawUi](GraphContext& c)
                    {
                        if (drawUi)
                            ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), c.cmd());
                    });
        if (shownByUi)
            ui.read(sceneImage, Usage::ShaderRead);
    }

    const bool dumpThisFrame = std::find(dumpFrames_.begin(), dumpFrames_.end(), frameCounter_) !=
                               dumpFrames_.end();
    if (dumpThisFrame)
        graph_.addPass("frame dump")
            .read(swapchain, Usage::TransferSrc)
            .execute([this, swapchain](GraphContext& c)
                     { recordDumpCopy(c.cmd(), c.image(swapchain)); });

    graph_.record(cmd);

    endGpuFrameTime(cmd, frame);
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
    vkDestroyQueryPool(ctx_.device_, frameQueries_, nullptr);
    vkDestroyCommandPool(ctx_.device_, commandPool_, nullptr);
}

}  // namespace batap
