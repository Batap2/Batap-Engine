#include "VulkanRenderer.h"

#include "VulkanMemory.h"
#include "VulkanScenePasses.h"

#include "Platform/PlatformWindow.h"
#include "Renderer/EngineConfig.h"

#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>

#if defined(__clang__)
  #pragma clang diagnostic push
  #pragma clang diagnostic ignored "-Weverything"
#endif
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#if defined(__clang__)
  #pragma clang diagnostic pop
#endif

#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace batap
{

namespace
{
constexpr VkFormat DepthFormat = VK_FORMAT_D32_SFLOAT;
}

Renderer::Renderer(void* nativeWindow, uint32_t clientWidth, uint32_t clientHeight,
                   bool transparent)
{
    width_ = clientWidth;
    height_ = clientHeight;
    transparent_ = transparent;

    ctx_.init();
    swapchain_.init(ctx_, platformSurfaceHandle(nativeWindow), transparent);
    // La taille de rendu est celle de la swapchain, en pixels physiques —
    // pas la taille client demandée (qui est en points sur un écran retina).
    width_ = swapchain_.extent_.width;
    height_ = swapchain_.extent_.height;
    createDepthBuffer();

    // Le renderer possède le ResourceManager ; l'Engine en distribue le
    // pointeur (AssetManager, InstanceManager) — même schéma que le DX12.
    resources_ = std::make_unique<ResourceManager>();
    resources_->init(ctx_);
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

    std::cout << "[Vulkan] Renderer prêt — swapchain " << swapchain_.extent_.width << "x"
              << swapchain_.extent_.height << std::endl;
}

// Enregistre la copie swapchain -> buffer de readback (l'image doit être en
// TRANSFER_SRC au moment de la copie)
void Renderer::recordDumpCopy(VkCommandBuffer cmd, uint32_t imageIndex)
{
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = uint64_t(swapchain_.extent_.width) * swapchain_.extent_.height * 4;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags =
        VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

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
    stbi_write_png("frame_dump.png", int(swapchain_.extent_.width),
                   int(swapchain_.extent_.height), 4, px, int(swapchain_.extent_.width * 4));
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
    imageInfo.extent = {swapchain_.extent_.width, swapchain_.extent_.height, 1};
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

void Renderer::initImGui()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    platformImGuiInit(window_);

    // Le backend dessine dans le rendering scope de la frame : sa pipeline
    // doit déclarer les mêmes attachements (couleur swapchain + depth).
    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion = VK_API_VERSION_1_3;
    info.Instance = ctx_.instance_;
    info.PhysicalDevice = ctx_.physicalDevice_;
    info.Device = ctx_.device_;
    info.QueueFamily = ctx_.graphicsQueueFamily_;
    info.Queue = ctx_.graphicsQueue_;
    info.DescriptorPoolSize = 64;  // pool interne (fontes + textures ImGui)
    info.MinImageCount = 2;
    info.ImageCount = static_cast<uint32_t>(swapchain_.images_.size());
    info.UseDynamicRendering = true;

    VkPipelineRenderingCreateInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &swapchain_.format_;  // deep-copié par le backend
    rendering.depthAttachmentFormat = DepthFormat;
    info.PipelineInfoMain.PipelineRenderingCreateInfo = rendering;

    if (!ImGui_ImplVulkan_Init(&info))
        throw std::runtime_error("Renderer(vk) : ImGui_ImplVulkan_Init");
}

ScenePasses* Renderer::scenePasses()
{
    if (!scenePasses_)
    {
        scenePasses_ = std::make_unique<ScenePasses>();
        scenePasses_->init(ctx_, *resources_, swapchain_.format_, DepthFormat);
    }
    return scenePasses_.get();
}

void Renderer::setSceneRecord(SceneRecordFn fn)
{
    sceneRecord_ = std::move(fn);
}

void Renderer::beginFrame()
{
    swapchain_.waitFrame();
    resources_->beginFrame();
    frameBegun_ = true;

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
    if (!frameBegun_)
        beginFrame();
    frameBegun_ = false;

    const uint32_t imageIndex = swapchain_.acquire();
    if (imageIndex == VulkanSwapchain::OutOfDate)
    {
        // Swapchain périmée entre deux événements de resize : on recrée et on
        // saute la frame (les uploads en attente partiront à la suivante).
        resize(0, 0);
        return;
    }
    const uint32_t frame = ctx_.frameIndex_;

    // Hot reload shaders : vérification espacée (stat de 4 fichiers), et
    // surtout hors enregistrement — la reconstruction attend l'idle GPU.
    if (scenePasses_ && frameCounter_ % 30 == 0)
        scenePasses_->checkHotReload(ctx_.device_);

    VkCommandBuffer cmd = commandBuffers_[frame];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    // Les uploads demandés pendant la frame CPU (assets, instances, arenas)
    resources_->flushUploads(cmd);

    // Swapchain -> color attachment, depth -> depth attachment (contenus
    // précédents jetés : loadOp CLEAR des deux côtés)
    VkImageMemoryBarrier2 barriers[2]{};
    barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barriers[0].srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    barriers[0].dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    barriers[0].dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    barriers[0].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barriers[0].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barriers[0].image = swapchain_.images_[imageIndex];
    barriers[0].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    barriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barriers[1].srcStageMask = VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    barriers[1].srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    barriers[1].dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT;
    barriers[1].dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    barriers[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barriers[1].newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    barriers[1].image = depthImage_;
    barriers[1].subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};

    VkDependencyInfo dep{};
    dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.imageMemoryBarrierCount = 2;
    dep.pImageMemoryBarriers = barriers;
    vkCmdPipelineBarrier2(cmd, &dep);

    VkRenderingAttachmentInfo color{};
    color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    color.imageView = swapchain_.views_[imageIndex];
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    // Alpha 0 en transparent : le bureau se voit là où rien n'est dessiné
    color.clearValue.color = {{0.0f, 0.0f, 0.0f, transparent_ ? 0.0f : 1.0f}};

    VkRenderingAttachmentInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depth.imageView = depthView_;
    depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.clearValue.depthStencil = {1.0f, 0};

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea = {{0, 0}, swapchain_.extent_};
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &color;
    renderingInfo.pDepthAttachment = &depth;

    vkCmdBeginRendering(cmd, &renderingInfo);
    if (sceneRecord_)
        sceneRecord_(cmd, frame, swapchain_.extent_.width, swapchain_.extent_.height);
    if (imguiFrameOpen_)
    {
        ImGui::Render();
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
        imguiFrameOpen_ = false;
    }
    vkCmdEndRendering(cmd);

    auto swapchainBarrier = [&](VkImageLayout oldLayout, VkImageLayout newLayout,
                                VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                                VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess)
    {
        VkImageMemoryBarrier2 b{};
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        b.srcStageMask = srcStage;
        b.srcAccessMask = srcAccess;
        b.dstStageMask = dstStage;
        b.dstAccessMask = dstAccess;
        b.oldLayout = oldLayout;
        b.newLayout = newLayout;
        b.image = swapchain_.images_[imageIndex];
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkDependencyInfo di{};
        di.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        di.imageMemoryBarrierCount = 1;
        di.pImageMemoryBarriers = &b;
        vkCmdPipelineBarrier2(cmd, &di);
    };

    const bool dumpThisFrame =
        dumpAtFrame_ >= 0 && frameCounter_ == static_cast<uint64_t>(dumpAtFrame_);
    if (dumpThisFrame)
    {
        swapchainBarrier(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COPY_BIT,
                         VK_ACCESS_2_TRANSFER_READ_BIT);
        recordDumpCopy(cmd, imageIndex);
        swapchainBarrier(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                         VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                         VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, 0);
    }
    else
    {
        swapchainBarrier(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                         VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                         VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, 0);
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

    swapchain_.recreate();  // attend l'idle GPU, suit la taille de la surface

    vkDestroyImageView(ctx_.device_, depthView_, nullptr);
    vmaDestroyImage(ctx_.allocator_, depthImage_, depthAllocation_);
    createDepthBuffer();

    // La swapchain fait foi (la surface peut clamper la taille demandée)
    width_ = swapchain_.extent_.width;
    height_ = swapchain_.extent_.height;

    for (auto& cb : resizeCallbacks_)
        cb(width_, height_);
}

void Renderer::onResize(ResizeCallback cb)
{
    resizeCallbacks_.push_back(std::move(cb));
}

void Renderer::flush()
{
    vkDeviceWaitIdle(ctx_.device_);
}

Renderer::~Renderer()
{
    vkDeviceWaitIdle(ctx_.device_);
    ImGui_ImplVulkan_Shutdown();
    platformImGuiShutdown();
    ImGui::DestroyContext();
    if (scenePasses_)
        scenePasses_->shutdown(ctx_.device_);
    vkDestroyImageView(ctx_.device_, depthView_, nullptr);
    vmaDestroyImage(ctx_.allocator_, depthImage_, depthAllocation_);
    vkDestroyCommandPool(ctx_.device_, commandPool_, nullptr);
    resources_->shutdown();
    swapchain_.shutdown();
    ctx_.shutdown();
}

}  // namespace batap
