#pragma once

#include <volk.h>

#include "Handles.h"
#include "Renderer/EngineConfig.h"
#include "Renderer/ResourceFormat.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

VK_DEFINE_HANDLE(VmaAllocator)
VK_DEFINE_HANDLE(VmaAllocation)

namespace batap
{
struct VulkanContext;

// Owns every GPU buffer and image, each behind one GPUResourceHandle.
// Nothing talks to the GPU on the spot: uploads are recorded once per frame by
// flushUploads, destroys happen a full frame cycle later.
struct ResourceManager
{
    explicit ResourceManager(VulkanContext& ctx,
                             uint64_t stagingBytesPerFrame = StagingBytesPerFrame);
    ~ResourceManager();

    ResourceManager(const ResourceManager&) = delete;
    ResourceManager& operator=(const ResourceManager&) = delete;

    GPUResourceHandle createStaticBuffer(uint64_t sizeBytes,
                                         std::optional<std::string_view> name = std::nullopt);
    GPUResourceHandle createPerFrameBuffer(uint64_t sizeBytes,
                                           std::optional<std::string_view> name = std::nullopt);
    GPUResourceHandle createImage2D(uint32_t width, uint32_t height, ResourceFormat format,
                                    std::optional<std::string_view> name = std::nullopt);

    GPUResourceHandle createTarget(uint32_t width, uint32_t height, ResourceFormat format,
                                   std::optional<std::string_view> name = std::nullopt);


    VkImage imageFor(GPUResourceHandle handle);

    void requestDestroy(GPUResourceHandle handle);
    uint32_t textureIndex(GPUResourceHandle image);

    // Returns 16-byte aligned staging memory for the caller to fill in place
    std::span<std::byte> requestUpload(GPUResourceHandle dest, uint64_t sizeBytes,
                                       uint64_t destOffset = 0);
    std::span<std::byte> requestPartialUpload(GPUResourceHandle dest, uint64_t sizeBytes,
                                              uint64_t destOffset, uint64_t subOffset,
                                              uint64_t subSize);
    std::span<std::byte> requestTextureUpload(GPUResourceHandle dest, uint32_t width,
                                              uint32_t height, ResourceFormat format);

    // Writes in place in the command stream, in order with the commands around
    // it, rather than through the staging ring that flushUploads empties at the
    // top of the frame
    void recordBufferWrite(VkCommandBuffer cmd, GPUResourceHandle dest, const void* data,
                           uint64_t sizeBytes, uint64_t destOffset = 0);

    void beginFrame();
    void flushUploads(VkCommandBuffer cmd);

    VkBuffer bufferFor(GPUResourceHandle handle);

    VkImageView viewFor(GPUResourceHandle handle);
    VkSampler textureSampler() const { return textureSampler_; }
    VkSampler shadowSampler() const { return shadowSampler_; }
    VkDevice device() const;

    VkDescriptorSetLayout textureSetLayout() const { return textureSetLayout_; }
    VkDescriptorSet textureSet() const { return textureSet_; }

   private:
    struct Buffer
    {
        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation allocation = nullptr;
        uint64_t size = 0;
    };

    struct Image
    {
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation allocation = nullptr;
        VkImageView view = VK_NULL_HANDLE;
        VkFormat format = VK_FORMAT_UNDEFINED;
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t mipLevels = 1;
        uint32_t textureIndex = 0;
    };

    // Mapped once for the whole run; offset bumps forward, rewinds at beginFrame.
    struct StagingRing
    {
        Buffer buffer;
        std::byte* mapped = nullptr;
        uint64_t offset = 0;
    };

    struct UploadRequest
    {
        GPUResourceHandle dest;
        VkBuffer src = VK_NULL_HANDLE;
        uint64_t srcOffset = 0;
        uint64_t size = 0;
        uint64_t dstOffset = 0;
        // images only
        bool isImage = false;
        uint32_t width = 0;
        uint32_t height = 0;
    };

    struct DestroyQueue
    {
        std::vector<Buffer> buffers;
        std::vector<Image> images;
    };

    Buffer createBufferInternal(uint64_t sizeBytes);
    Buffer createStagingBuffer(uint64_t sizeBytes, std::byte*& outMapped);
    std::byte* stagingAlloc(uint64_t size, UploadRequest& req);

    uint32_t allocTextureIndex();
    void destroyNow(Buffer& b);
    void destroyNow(Image& i);

    uint32_t currentFrame() const;

    Buffer* getBuffer(GPUResourceHandle handle);
    Buffer* getFrameBuffer(GPUResourceHandle handle, uint32_t frame);
    Image* getImage(GPUResourceHandle handle);

    VulkanContext& ctx_;
    VmaAllocator allocator_ = nullptr;

    // A handle lives in exactly one of these.
    std::unordered_map<GPUResourceHandle, Buffer> buffers_;
    std::unordered_map<GPUResourceHandle, std::vector<Buffer>> frameBuffers_;
    std::unordered_map<GPUResourceHandle, Image> images_;

    // Indexed by currentFrame().
    std::vector<StagingRing> staging_;
    std::vector<DestroyQueue> destroyQueues_;
    std::vector<UploadRequest> uploadRequests_;
    // Requests that did not fit the ring; flushUploads hands them to destroyQueues_.
    std::vector<Buffer> oversizeStaging_;

    // Set 0: the default sampler plus the bindless table of sampled images.
    // textureNext_ hands out never-used slots, textureFree_ recycles freed ones.
    VkDescriptorSetLayout textureSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorSet textureSet_ = VK_NULL_HANDLE;
    VkDescriptorPool texturePool_ = VK_NULL_HANDLE;
    VkSampler textureSampler_ = VK_NULL_HANDLE;
    VkSampler shadowSampler_ = VK_NULL_HANDLE;
    uint32_t textureCapacity_ = 0;
    uint32_t textureNext_ = 0;
    std::vector<uint32_t> textureFree_;
};
}  // namespace batap
