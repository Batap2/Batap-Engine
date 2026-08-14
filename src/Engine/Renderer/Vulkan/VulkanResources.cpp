#include "VulkanResources.h"

#include "VulkanContext.h"
#include "VulkanFormats.h"
#include "VulkanMemory.h"

#include <cassert>
#include <cstring>
#include <stdexcept>

namespace batap
{

namespace
{
constexpr uint32_t SamplerBinding = 0;
constexpr uint32_t TexturesBinding = 1;

uint64_t alignUp(uint64_t v, uint64_t a)
{
    if (a <= 1)
        return v;
    return (v + (a - 1)) & ~(a - 1);
}
}  // namespace

void ResourceManager::init(VulkanContext& ctx, uint32_t framesInFlight,
                           uint64_t stagingBytesPerFrame)
{
    ctx_ = &ctx;
    allocator_ = ctx.allocator_;
    framesInFlight_ = framesInFlight;
    destroyQueues_.resize(framesInFlight);

    // ---- Staging rings, mappés en permanence ----
    staging_.resize(framesInFlight);
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

    // ---- Sampler par défaut (l'équivalent du static sampler s0 du moteur) ----
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
    if (vkCreateSampler(ctx_->device_, &samplerInfo, nullptr, &defaultSampler_) != VK_SUCCESS)
        throw std::runtime_error("ResourceManager(vk) : sampler");

    // ---- Set bindless global ----
    // binding 0 : sampler immutable ; binding 1 : tableau de sampled images à
    // taille variable, partially bound, update-after-bind (le binding à taille
    // variable doit être le dernier du set — contrainte de la spec).
    bindlessCapacity_ = BindlessTextureCapacity;

    VkDescriptorSetLayoutBinding bindings[2]{};
    bindings[SamplerBinding].binding = SamplerBinding;
    bindings[SamplerBinding].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    bindings[SamplerBinding].descriptorCount = 1;
    bindings[SamplerBinding].stageFlags = VK_SHADER_STAGE_ALL;
    bindings[SamplerBinding].pImmutableSamplers = &defaultSampler_;

    bindings[TexturesBinding].binding = TexturesBinding;
    bindings[TexturesBinding].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    bindings[TexturesBinding].descriptorCount = bindlessCapacity_;
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
    if (vkCreateDescriptorSetLayout(ctx_->device_, &layoutInfo, nullptr, &bindlessLayout_) !=
        VK_SUCCESS)
        throw std::runtime_error("ResourceManager(vk) : bindless layout");

    VkDescriptorPoolSize poolSizes[2] = {
        {VK_DESCRIPTOR_TYPE_SAMPLER, 1},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, bindlessCapacity_},
    };
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;
    if (vkCreateDescriptorPool(ctx_->device_, &poolInfo, nullptr, &bindlessPool_) != VK_SUCCESS)
        throw std::runtime_error("ResourceManager(vk) : bindless pool");

    VkDescriptorSetVariableDescriptorCountAllocateInfo variableInfo{};
    variableInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO;
    variableInfo.descriptorSetCount = 1;
    variableInfo.pDescriptorCounts = &bindlessCapacity_;

    VkDescriptorSetAllocateInfo setInfo{};
    setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    setInfo.pNext = &variableInfo;
    setInfo.descriptorPool = bindlessPool_;
    setInfo.descriptorSetCount = 1;
    setInfo.pSetLayouts = &bindlessLayout_;
    if (vkAllocateDescriptorSets(ctx_->device_, &setInfo, &bindlessSet_) != VK_SUCCESS)
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
    meshViews_.clear();
    viewToResource_.clear();

    if (bindlessPool_)
        vkDestroyDescriptorPool(ctx_->device_, bindlessPool_, nullptr);
    if (bindlessLayout_)
        vkDestroyDescriptorSetLayout(ctx_->device_, bindlessLayout_, nullptr);
    if (defaultSampler_)
        vkDestroySampler(ctx_->device_, defaultSampler_, nullptr);
    bindlessPool_ = VK_NULL_HANDLE;
    bindlessLayout_ = VK_NULL_HANDLE;
    bindlessSet_ = VK_NULL_HANDLE;
    defaultSampler_ = VK_NULL_HANDLE;
}

ResourceManager::Buffer ResourceManager::createBufferInternal(uint64_t sizeBytes)
{
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = sizeBytes;
    // Usage générique : le contrat neutre ne précise pas l'usage, et le coût
    // d'un usage large est nul en pratique.
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

ResourceManager::StructuredBuffer
ResourceManager::createStaticStructuredBuffer(uint64_t elementCount, uint32_t elementStride,
                                              std::optional<std::string_view> name)
{
    StructuredBuffer out;
    out.resource = createStaticBuffer(elementCount * elementStride, name);
    out.srv = GPUViewHandle(GPUViewType::StaticView);
    viewToResource_.emplace(out.srv, out.resource);
    return out;
}

ResourceManager::StructuredBuffer
ResourceManager::createFrameStructuredBuffer(uint64_t elementCount, uint32_t elementStride,
                                             std::optional<std::string_view> name)
{
    StructuredBuffer out;
    out.resource = name ? GPUResourceHandle(GPUResourceType::FrameResource, *name)
                        : GPUResourceHandle(GPUResourceType::FrameResource);

    auto& buffers = frameBuffers_[out.resource];
    buffers.reserve(framesInFlight_);
    for (uint32_t i = 0; i < framesInFlight_; ++i)
        buffers.push_back(createBufferInternal(elementCount * elementStride));

    out.srv = GPUViewHandle(GPUViewType::FrameView);
    viewToResource_.emplace(out.srv, out.resource);
    return out;
}

ResourceManager::Texture2D ResourceManager::createTexture2D(uint32_t width, uint32_t height,
                                                            ResourceFormat format,
                                                            std::optional<std::string_view> name)
{
    Texture2D out;
    out.resource = name ? GPUResourceHandle(GPUResourceType::StaticResource, *name)
                        : GPUResourceHandle(GPUResourceType::StaticResource);

    Image image{};
    image.format = toVkFormat(format);
    image.width = width;
    image.height = height;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = image.format;
    imageInfo.extent = {width, height, 1};
    imageInfo.mipLevels = 1;
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
        throw std::runtime_error("ResourceManager(vk) : createTexture2D");

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = image.format;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(ctx_->device_, &viewInfo, nullptr, &image.view) != VK_SUCCESS)
        throw std::runtime_error("ResourceManager(vk) : image view");

    // Slot bindless : écrit immédiatement (update-after-bind) ; la barrière de
    // flushUploads mettra l'image dans le bon layout avant tout sampling.
    image.bindlessIndex = allocBindlessIndex();

    VkDescriptorImageInfo descriptor{};
    descriptor.imageView = image.view;
    descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = bindlessSet_;
    write.dstBinding = TexturesBinding;
    write.dstArrayElement = image.bindlessIndex;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    write.pImageInfo = &descriptor;
    vkUpdateDescriptorSets(ctx_->device_, 1, &write, 0, nullptr);

    out.bindlessIndex = image.bindlessIndex;
    out.srv = GPUViewHandle(GPUViewType::StaticView);
    viewToResource_.emplace(out.srv, out.resource);
    images_.emplace(out.resource, image);
    return out;
}

GPUMeshViewHandle ResourceManager::createStaticIBV(GPUResourceHandle resource,
                                                   ResourceFormat format,
                                                   std::optional<std::string_view> name,
                                                   uint64_t offset, uint64_t size)
{
    GPUMeshViewHandle handle = name
        ? GPUMeshViewHandle(GPUMeshViewType::StaticMeshView, *name)
        : GPUMeshViewHandle(GPUMeshViewType::StaticMeshView);

    MeshView view{};
    view.resource = resource;
    view.offset = offset;
    view.size = size;
    view.isIndex = true;
    view.indexType = (format == ResourceFormat::R16_UINT) ? VK_INDEX_TYPE_UINT16
                                                          : VK_INDEX_TYPE_UINT32;
    meshViews_.emplace(handle, view);
    return handle;
}

GPUMeshViewHandle ResourceManager::createStaticVBV(GPUResourceHandle resource,
                                                   uint32_t strideBytes,
                                                   std::optional<std::string_view> name,
                                                   uint64_t offset, uint64_t size)
{
    GPUMeshViewHandle handle = name
        ? GPUMeshViewHandle(GPUMeshViewType::StaticMeshView, *name)
        : GPUMeshViewHandle(GPUMeshViewType::StaticMeshView);

    MeshView view{};
    view.resource = resource;
    view.offset = offset;
    view.size = size;
    view.stride = strideBytes;
    meshViews_.emplace(handle, view);
    return handle;
}

uint32_t ResourceManager::bindlessIndex(GPUViewHandle view)
{
    auto it = viewToResource_.find(view);
    assert(it != viewToResource_.end() && "bindlessIndex : view inconnue");
    auto imageIt = images_.find(it->second);
    assert(imageIt != images_.end() && "bindlessIndex : pas une texture");
    return imageIt->second.bindlessIndex;
}

std::byte* ResourceManager::stagingAlloc(uint64_t size, uint64_t alignment, uint64_t& outOffset)
{
    auto& ring = staging_[currentFrame_];
    const uint64_t offset = alignUp(ring.offset, alignment);
    if (offset + size > ring.buffer.size)
        throw std::runtime_error(
            "ResourceManager(vk) : staging ring plein — augmenter stagingBytesPerFrame");
    ring.offset = offset + size;
    outOffset = offset;
    return ring.mapped + offset;
}

std::span<std::byte> ResourceManager::requestUploadOwned(GPUResourceHandle dest,
                                                         uint64_t dataSize, uint32_t alignment,
                                                         uint64_t destinationOffset,
                                                         uint64_t subRegionOffset,
                                                         uint64_t subRegionSize)
{
    UploadRequest req{};
    req.dest = dest;
    req.dstOffset = destinationOffset;
    std::byte* ptr = stagingAlloc(dataSize, std::max<uint64_t>(alignment, 4), req.srcOffset);
    // Seule la sous-région est copiée ; le span rendu couvre tout dataSize
    // (l'appelant y écrit la struct complète, cf. InstanceManager)
    req.srcOffset += subRegionOffset;
    req.size = subRegionSize ? subRegionSize : dataSize;
    uploadRequests_.push_back(req);
    return {ptr, dataSize};
}

std::span<std::byte> ResourceManager::requestUploadOwned(GPUViewHandle dest, uint64_t dataSize,
                                                         uint32_t alignment,
                                                         uint64_t destinationOffset,
                                                         uint64_t subRegionOffset,
                                                         uint64_t subRegionSize)
{
    auto it = viewToResource_.find(dest);
    assert(it != viewToResource_.end() && "requestUploadOwned : view inconnue");
    return requestUploadOwned(it->second, dataSize, alignment, destinationOffset, subRegionOffset,
                              subRegionSize);
}

std::span<std::byte> ResourceManager::requestTextureUploadOwned(GPUResourceHandle dest,
                                                                uint32_t width, uint32_t height,
                                                                ResourceFormat format,
                                                                uint32_t& outRowPitch)
{
    // Pas d'alignement de row pitch en Vulkan (le 256 était du DX12)
    const uint32_t bpp = bytesPerPixel(format);
    const uint64_t size = uint64_t(width) * bpp * height;

    UploadRequest req{};
    req.dest = dest;
    req.size = size;
    req.isTexture = true;
    req.texWidth = width;
    req.texHeight = height;
    std::byte* ptr = stagingAlloc(size, std::max<uint64_t>(16, bpp), req.srcOffset);
    uploadRequests_.push_back(req);

    outRowPitch = width * bpp;
    return {ptr, size};
}

void ResourceManager::flushUploads(VkCommandBuffer cmd, uint32_t frameIndex)
{
    if (uploadRequests_.empty())
        return;

    for (auto& req : uploadRequests_)
    {
        if (!req.isTexture)
        {
            VkBuffer target = VK_NULL_HANDLE;
            if (auto it = buffers_.find(req.dest); it != buffers_.end())
                target = it->second.buffer;
            else if (auto fit = frameBuffers_.find(req.dest); fit != frameBuffers_.end())
                target = fit->second[frameIndex].buffer;
            assert(target && "flushUploads : buffer inconnu");

            VkBufferCopy region{};
            region.srcOffset = req.srcOffset;
            region.dstOffset = req.dstOffset;
            region.size = req.size;
            vkCmdCopyBuffer(cmd, staging_[currentFrame_].buffer.buffer, target, 1, &region);
            continue;
        }

        auto it = images_.find(req.dest);
        assert(it != images_.end() && "flushUploads : image inconnue");
        Image& image = it->second;

        VkImageMemoryBarrier2 toDst{};
        toDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        toDst.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        toDst.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        toDst.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        toDst.oldLayout = image.layout;
        toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toDst.image = image.image;
        toDst.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        VkDependencyInfo depDst{};
        depDst.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        depDst.imageMemoryBarrierCount = 1;
        depDst.pImageMemoryBarriers = &toDst;
        vkCmdPipelineBarrier2(cmd, &depDst);

        VkBufferImageCopy region{};
        region.bufferOffset = req.srcOffset;
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {req.texWidth, req.texHeight, 1};
        vkCmdCopyBufferToImage(cmd, staging_[currentFrame_].buffer.buffer, image.image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        VkImageMemoryBarrier2 toRead = toDst;
        toRead.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        toRead.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        toRead.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        toRead.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
        toRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkDependencyInfo depRead{};
        depRead.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        depRead.imageMemoryBarrierCount = 1;
        depRead.pImageMemoryBarriers = &toRead;
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

void ResourceManager::beginFrame(uint32_t frameIndex)
{
    currentFrame_ = frameIndex;
    // Ne recycle le ring que si tout ce qui y a été écrit a été flushé —
    // des requêtes peuvent précéder la première frame (createDefaultAssets).
    if (uploadRequests_.empty())
        staging_[frameIndex].offset = 0;

    auto& queue = destroyQueues_[frameIndex];
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
        destroyQueues_[currentFrame_].buffers.push_back(it->second);
        buffers_.erase(it);
        return;
    }
    if (auto it = frameBuffers_.find(handle); it != frameBuffers_.end())
    {
        for (auto& b : it->second)
            destroyQueues_[currentFrame_].buffers.push_back(b);
        frameBuffers_.erase(it);
        return;
    }
    if (auto it = images_.find(handle); it != images_.end())
    {
        bindlessFree_.push_back(it->second.bindlessIndex);
        destroyQueues_[currentFrame_].images.push_back(it->second);
        images_.erase(it);
    }
}

void ResourceManager::requestDestroy(GPUViewHandle handle, bool destroyAssociatedResources)
{
    if (auto it = viewToResource_.find(handle); it != viewToResource_.end())
    {
        if (destroyAssociatedResources)
            requestDestroy(it->second);
        viewToResource_.erase(it);
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

ResourceManager::MeshView* ResourceManager::getMeshView(GPUMeshViewHandle handle)
{
    auto it = meshViews_.find(handle);
    return it != meshViews_.end() ? &it->second : nullptr;
}

VkBuffer ResourceManager::bufferForView(GPUViewHandle view, uint32_t frame)
{
    auto it = viewToResource_.find(view);
    assert(it != viewToResource_.end() && "bufferForView : view inconnue");
    if (auto* b = getBuffer(it->second))
        return b->buffer;
    if (auto* b = getFrameBuffer(it->second, frame))
        return b->buffer;
    assert(false && "bufferForView : pas un buffer");
    return VK_NULL_HANDLE;
}

VkDevice ResourceManager::device() const
{
    return ctx_->device_;
}

uint32_t ResourceManager::allocBindlessIndex()
{
    if (!bindlessFree_.empty())
    {
        const uint32_t idx = bindlessFree_.back();
        bindlessFree_.pop_back();
        return idx;
    }
    assert(bindlessNext_ < bindlessCapacity_ && "bindless plein");
    return bindlessNext_++;
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
