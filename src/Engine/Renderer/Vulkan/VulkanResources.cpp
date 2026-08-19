#include "VulkanResources.h"

#include "VulkanContext.h"
#include "VulkanFormats.h"
#include "VulkanMemory.h"

#include <algorithm>
#include <bit>
#include <cassert>
#include <cstring>
#include <stdexcept>

namespace batap
{

namespace
{
constexpr uint32_t SamplerBinding = 0;
constexpr uint32_t TexturesBinding = 1;

// Every staging allocation is aligned to this — see stagingAlloc.
constexpr uint64_t StagingAlignment = 16;

uint64_t alignUp(uint64_t v, uint64_t a)
{
    assert(std::has_single_bit(a) && "alignUp: alignment must be a power of two");
    return (v + (a - 1)) & ~(a - 1);
}
}  // namespace

void ResourceManager::init(VulkanContext& ctx, uint64_t stagingBytesPerFrame)
{
    ctx_ = &ctx;
    allocator_ = ctx.allocator_;
    destroyQueues_.resize(FramesInFlight);

    // ---- Staging rings, mappés en permanence ----
    staging_.resize(FramesInFlight);
    for (auto& ring : staging_)
    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = stagingBytesPerFrame;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                          VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VmaAllocationInfo outInfo{};
        if (vmaCreateBuffer(allocator_, &bufferInfo, &allocInfo, &ring.buffer.buffer,
                            &ring.buffer.allocation, &outInfo) != VK_SUCCESS)
            throw std::runtime_error("ResourceManager(vk) : staging ring");
        ring.buffer.size = stagingBytesPerFrame;
        ring.mapped = static_cast<std::byte*>(outInfo.pMappedData);
    }

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
    // Keeps oblique surfaces sharp — without it, trilinear over the mip chain
    // turns grazing angles blurry.
    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy = 16.0f;
    if (vkCreateSampler(ctx_->device_, &samplerInfo, nullptr, &textureSampler_) != VK_SUCCESS)
        throw std::runtime_error("ResourceManager(vk) : sampler");

    textureCapacity_ = BindlessTextureCapacity;

    VkDescriptorSetLayoutBinding bindings[2]{};
    bindings[SamplerBinding].binding = SamplerBinding;
    bindings[SamplerBinding].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    bindings[SamplerBinding].descriptorCount = 1;
    bindings[SamplerBinding].stageFlags = VK_SHADER_STAGE_ALL;
    bindings[SamplerBinding].pImmutableSamplers = &textureSampler_;

    // VARIABLE_DESCRIPTOR_COUNT requires the highest binding number of the set
    bindings[TexturesBinding].binding = TexturesBinding;
    bindings[TexturesBinding].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    bindings[TexturesBinding].descriptorCount = textureCapacity_;
    bindings[TexturesBinding].stageFlags = VK_SHADER_STAGE_ALL;

    const VkDescriptorBindingFlags textureFlags =
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
        VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT |
        VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
    const VkDescriptorBindingFlags bindingFlags[2] = {0, textureFlags};

    VkDescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{};
    flagsInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
    flagsInfo.bindingCount = 2;
    flagsInfo.pBindingFlags = bindingFlags;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.pNext = &flagsInfo;
    layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    layoutInfo.bindingCount = 2;
    layoutInfo.pBindings = bindings;
    if (vkCreateDescriptorSetLayout(ctx_->device_, &layoutInfo, nullptr, &textureSetLayout_) !=
        VK_SUCCESS)
        throw std::runtime_error("ResourceManager(vk) : bindless layout");

    VkDescriptorPoolSize poolSizes[2] = {
        {VK_DESCRIPTOR_TYPE_SAMPLER, 1},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, textureCapacity_},
    };
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;
    if (vkCreateDescriptorPool(ctx_->device_, &poolInfo, nullptr, &texturePool_) != VK_SUCCESS)
        throw std::runtime_error("ResourceManager(vk) : bindless pool");

    VkDescriptorSetVariableDescriptorCountAllocateInfo variableInfo{};
    variableInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO;
    variableInfo.descriptorSetCount = 1;
    variableInfo.pDescriptorCounts = &textureCapacity_;

    VkDescriptorSetAllocateInfo setInfo{};
    setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    setInfo.pNext = &variableInfo;
    setInfo.descriptorPool = texturePool_;
    setInfo.descriptorSetCount = 1;
    setInfo.pSetLayouts = &textureSetLayout_;
    if (vkAllocateDescriptorSets(ctx_->device_, &setInfo, &textureSet_) != VK_SUCCESS)
        throw std::runtime_error("ResourceManager(vk) : bindless set");
}

void ResourceManager::shutdown()
{
    for (auto& [handle, buffer] : buffers_)
        destroyNow(buffer);
    buffers_.clear();
    for (auto& [handle, buffers] : frameBuffers_)
        for (auto& b : buffers)
            destroyNow(b);
    frameBuffers_.clear();
    for (auto& [handle, image] : images_)
        destroyNow(image);
    images_.clear();
    for (auto& queue : destroyQueues_)
    {
        for (auto& b : queue.buffers)
            destroyNow(b);
        for (auto& i : queue.images)
            destroyNow(i);
    }
    destroyQueues_.clear();
    for (auto& ring : staging_)
        destroyNow(ring.buffer);
    staging_.clear();

    if (texturePool_)
        vkDestroyDescriptorPool(ctx_->device_, texturePool_, nullptr);
    if (textureSetLayout_)
        vkDestroyDescriptorSetLayout(ctx_->device_, textureSetLayout_, nullptr);
    if (textureSampler_)
        vkDestroySampler(ctx_->device_, textureSampler_, nullptr);
    texturePool_ = VK_NULL_HANDLE;
    textureSetLayout_ = VK_NULL_HANDLE;
    textureSet_ = VK_NULL_HANDLE;
    textureSampler_ = VK_NULL_HANDLE;
}

ResourceManager::Buffer ResourceManager::createBufferInternal(uint64_t sizeBytes)
{
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = sizeBytes;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                       VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    Buffer buffer{};
    buffer.size = sizeBytes;
    if (vmaCreateBuffer(allocator_, &bufferInfo, &allocInfo, &buffer.buffer, &buffer.allocation,
                        nullptr) != VK_SUCCESS)
        throw std::runtime_error("ResourceManager(vk) : createBuffer");
    return buffer;
}

GPUResourceHandle ResourceManager::createStaticBuffer(uint64_t sizeBytes,
                                                      std::optional<std::string_view> name)
{
    GPUResourceHandle handle = name
        ? GPUResourceHandle(GPUResourceType::StaticResource, *name)
        : GPUResourceHandle(GPUResourceType::StaticResource);
    buffers_.emplace(handle, createBufferInternal(sizeBytes));
    return handle;
}

GPUResourceHandle ResourceManager::createPerFrameBuffer(uint64_t sizeBytes,
                                                        std::optional<std::string_view> name)
{
    GPUResourceHandle handle = name ? GPUResourceHandle(GPUResourceType::FrameResource, *name)
                                    : GPUResourceHandle(GPUResourceType::FrameResource);

    auto& buffers = frameBuffers_[handle];
    buffers.reserve(FramesInFlight);
    for (uint32_t i = 0; i < FramesInFlight; ++i)
        buffers.push_back(createBufferInternal(sizeBytes));

    return handle;
}

GPUResourceHandle ResourceManager::createImage2D(uint32_t width, uint32_t height,
                                                 ResourceFormat format,
                                                 std::optional<std::string_view> name)
{
    GPUResourceHandle handle = name ? GPUResourceHandle(GPUResourceType::StaticResource, *name)
                                    : GPUResourceHandle(GPUResourceType::StaticResource);

    Image image{};
    image.format = toVkFormat(format);
    image.width = width;
    image.height = height;
    // Full chain down to 1×1; flushUploads generates the levels by blit.
    image.mipLevels = static_cast<uint32_t>(std::bit_width(std::max(width, height)));

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = image.format;
    imageInfo.extent = {width, height, 1};
    imageInfo.mipLevels = image.mipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                      VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    if (vmaCreateImage(allocator_, &imageInfo, &allocInfo, &image.image, &image.allocation,
                       nullptr) != VK_SUCCESS)
        throw std::runtime_error("ResourceManager(vk) : createImage2D");

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = image.format;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, image.mipLevels, 0, 1};
    if (vkCreateImageView(ctx_->device_, &viewInfo, nullptr, &image.view) != VK_SUCCESS)
        throw std::runtime_error("ResourceManager(vk) : image view");

    image.textureIndex = allocTextureIndex();

    VkDescriptorImageInfo descriptor{};
    descriptor.imageView = image.view;
    descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = textureSet_;
    write.dstBinding = TexturesBinding;
    write.dstArrayElement = image.textureIndex;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    write.pImageInfo = &descriptor;
    vkUpdateDescriptorSets(ctx_->device_, 1, &write, 0, nullptr);

    images_.emplace(handle, image);
    return handle;
}

uint32_t ResourceManager::textureIndex(GPUResourceHandle texture)
{
    auto it = images_.find(texture);
    assert(it != images_.end() && "textureIndex: not a texture");
    return it->second.textureIndex;
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage"
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage-in-container"

std::byte* ResourceManager::stagingAlloc(uint64_t size, uint64_t& outOffset)
{
    auto& ring = staging_[currentFrame()];
    const uint64_t offset = alignUp(ring.offset, StagingAlignment);
    if (offset + size > ring.buffer.size)
        throw std::runtime_error(
            "ResourceManager(vk) : staging ring full — raise stagingBytesPerFrame");
    ring.offset = offset + size;
    outOffset = offset;
    return ring.mapped + offset;
}

std::span<std::byte> ResourceManager::requestUpload(GPUResourceHandle dest, uint64_t sizeBytes,
                                                    uint64_t destOffset)
{
    return requestPartialUpload(dest, sizeBytes, destOffset, 0, sizeBytes);
}

std::span<std::byte> ResourceManager::requestPartialUpload(GPUResourceHandle dest,
                                                           uint64_t sizeBytes,
                                                           uint64_t destOffset,
                                                           uint64_t subOffset, uint64_t subSize)
{
    assert(subOffset + subSize <= sizeBytes && "requestPartialUpload: sub-range out of the span");

    UploadRequest req{};
    req.dest = dest;
    req.dstOffset = destOffset;
    std::byte* ptr = stagingAlloc(sizeBytes, req.srcOffset);
    req.srcOffset += subOffset;
    req.size = subSize;
    uploadRequests_.push_back(req);
    return {ptr, sizeBytes};
}

std::span<std::byte> ResourceManager::requestTextureUpload(GPUResourceHandle dest, uint32_t width,
                                                           uint32_t height, ResourceFormat format)
{
    const uint64_t size = uint64_t(width) * bytesPerPixel(format) * height;

    UploadRequest req{};
    req.dest = dest;
    req.size = size;
    req.isImage = true;
    req.width = width;
    req.height = height;
    std::byte* ptr = stagingAlloc(size, req.srcOffset);
    uploadRequests_.push_back(req);

    return {ptr, size};
}

#pragma clang diagnostic pop

void ResourceManager::flushUploads(VkCommandBuffer cmd)
{
    if (uploadRequests_.empty())
        return;

    for (auto& req : uploadRequests_)
    {
        if (!req.isImage)
        {
            VkBuffer target = VK_NULL_HANDLE;
            if (auto it = buffers_.find(req.dest); it != buffers_.end())
                target = it->second.buffer;
            else if (auto fit = frameBuffers_.find(req.dest); fit != frameBuffers_.end())
                target = fit->second[currentFrame()].buffer;
            assert(target && "flushUploads: unknown buffer");

            VkBufferCopy region{};
            region.srcOffset = req.srcOffset;
            region.dstOffset = req.dstOffset;
            region.size = req.size;
            vkCmdCopyBuffer(cmd, staging_[currentFrame()].buffer.buffer, target, 1, &region);
            continue;
        }

        auto it = images_.find(req.dest);
        assert(it != images_.end() && "flushUploads: unknown image");
        Image& image = it->second;

        // Whole mip chain → TRANSFER_DST (mip 0 gets the copy, the rest the blits)
        VkImageMemoryBarrier2 toDst{};
        toDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        toDst.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        toDst.dstStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
        toDst.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        toDst.oldLayout = image.layout;
        toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toDst.image = image.image;
        toDst.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, image.mipLevels, 0, 1};

        VkDependencyInfo depDst{};
        depDst.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        depDst.imageMemoryBarrierCount = 1;
        depDst.pImageMemoryBarriers = &toDst;
        vkCmdPipelineBarrier2(cmd, &depDst);

        VkBufferImageCopy region{};
        region.bufferOffset = req.srcOffset;
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {req.width, req.height, 1};
        vkCmdCopyBufferToImage(cmd, staging_[currentFrame()].buffer.buffer, image.image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        // Each level is downscaled from the previous one: mip N-1 flips to
        // TRANSFER_SRC (waiting for the write that filled it), then the blit
        // reads it and writes mip N. Levels end up SRC except the last (DST).
        for (uint32_t mip = 1; mip < image.mipLevels; ++mip)
        {
            VkImageMemoryBarrier2 srcReady = toDst;
            srcReady.srcStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
            srcReady.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            srcReady.dstStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT;
            srcReady.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
            srcReady.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            srcReady.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            srcReady.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, mip - 1, 1, 0, 1};

            VkDependencyInfo depSrc{};
            depSrc.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            depSrc.imageMemoryBarrierCount = 1;
            depSrc.pImageMemoryBarriers = &srcReady;
            vkCmdPipelineBarrier2(cmd, &depSrc);

            const auto mipDim = [](uint32_t base, uint32_t level)
            { return static_cast<int32_t>(std::max(base >> level, 1u)); };

            VkImageBlit2 blit{};
            blit.sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2;
            blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip - 1, 0, 1};
            blit.srcOffsets[1] = {mipDim(image.width, mip - 1), mipDim(image.height, mip - 1), 1};
            blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip, 0, 1};
            blit.dstOffsets[1] = {mipDim(image.width, mip), mipDim(image.height, mip), 1};

            VkBlitImageInfo2 blitInfo{};
            blitInfo.sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2;
            blitInfo.srcImage = image.image;
            blitInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            blitInfo.dstImage = image.image;
            blitInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            blitInfo.regionCount = 1;
            blitInfo.pRegions = &blit;
            blitInfo.filter = VK_FILTER_LINEAR;
            vkCmdBlitImage2(cmd, &blitInfo);
        }

        // Everything → SHADER_READ_ONLY : mips [0, N-1) come from TRANSFER_SRC,
        // the last one from TRANSFER_DST.
        VkImageMemoryBarrier2 toRead[2]{};
        toRead[0] = toDst;
        toRead[0].srcStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
        toRead[0].srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        toRead[0].dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        toRead[0].dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
        toRead[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toRead[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        toRead[0].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, image.mipLevels - 1, 1, 0, 1};

        toRead[1] = toRead[0];
        toRead[1].srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        toRead[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        toRead[1].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, image.mipLevels - 1, 0, 1};

        VkDependencyInfo depRead{};
        depRead.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        depRead.imageMemoryBarrierCount = image.mipLevels > 1 ? 2u : 1u;
        depRead.pImageMemoryBarriers = toRead;
        vkCmdPipelineBarrier2(cmd, &depRead);

        image.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    VkMemoryBarrier2 memBarrier{};
    memBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    memBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    memBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    memBarrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    memBarrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT;

    VkDependencyInfo dep{};
    dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.memoryBarrierCount = 1;
    dep.pMemoryBarriers = &memBarrier;
    vkCmdPipelineBarrier2(cmd, &dep);

    uploadRequests_.clear();
}

void ResourceManager::beginFrame()
{
    // Ne recycle le ring que si tout ce qui y a été écrit a été flushé —
    // des requêtes peuvent précéder la première frame (createDefaultAssets).
    if (uploadRequests_.empty())
        staging_[currentFrame()].offset = 0;

    auto& queue = destroyQueues_[currentFrame()];
    for (auto& b : queue.buffers)
        destroyNow(b);
    for (auto& i : queue.images)
        destroyNow(i);
    queue.buffers.clear();
    queue.images.clear();
}

void ResourceManager::requestDestroy(GPUResourceHandle handle)
{
    if (auto it = buffers_.find(handle); it != buffers_.end())
    {
        destroyQueues_[currentFrame()].buffers.push_back(it->second);
        buffers_.erase(it);
        return;
    }
    if (auto it = frameBuffers_.find(handle); it != frameBuffers_.end())
    {
        for (auto& b : it->second)
            destroyQueues_[currentFrame()].buffers.push_back(b);
        frameBuffers_.erase(it);
        return;
    }
    if (auto it = images_.find(handle); it != images_.end())
    {
        textureFree_.push_back(it->second.textureIndex);
        destroyQueues_[currentFrame()].images.push_back(it->second);
        images_.erase(it);
    }
}

ResourceManager::Buffer* ResourceManager::getBuffer(GPUResourceHandle handle)
{
    auto it = buffers_.find(handle);
    return it != buffers_.end() ? &it->second : nullptr;
}

ResourceManager::Buffer* ResourceManager::getFrameBuffer(GPUResourceHandle handle, uint32_t frame)
{
    auto it = frameBuffers_.find(handle);
    return it != frameBuffers_.end() ? &it->second[frame] : nullptr;
}

ResourceManager::Image* ResourceManager::getImage(GPUResourceHandle handle)
{
    auto it = images_.find(handle);
    return it != images_.end() ? &it->second : nullptr;
}

VkBuffer ResourceManager::bufferFor(GPUResourceHandle handle)
{
    if (auto* b = getBuffer(handle))
        return b->buffer;
    if (auto* b = getFrameBuffer(handle, currentFrame()))
        return b->buffer;
    assert(false && "bufferFor: not a buffer");
    return VK_NULL_HANDLE;
}

VkDevice ResourceManager::device() const
{
    return ctx_->device_;
}

uint32_t ResourceManager::currentFrame() const
{
    return ctx_->frameIndex_;
}

uint32_t ResourceManager::allocTextureIndex()
{
    if (!textureFree_.empty())
    {
        const uint32_t idx = textureFree_.back();
        textureFree_.pop_back();
        return idx;
    }
    assert(textureNext_ < textureCapacity_ && "bindless table full");
    return textureNext_++;
}

void ResourceManager::destroyNow(Buffer& b)
{
    if (b.buffer)
        vmaDestroyBuffer(allocator_, b.buffer, b.allocation);
    b.buffer = VK_NULL_HANDLE;
}

void ResourceManager::destroyNow(Image& i)
{
    if (i.view)
        vkDestroyImageView(ctx_->device_, i.view, nullptr);
    if (i.image)
        vmaDestroyImage(allocator_, i.image, i.allocation);
    i.view = VK_NULL_HANDLE;
    i.image = VK_NULL_HANDLE;
}

}  // namespace batap
