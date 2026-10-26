#include "Renderer/Scene/FrameBindings.h"

#include "DebugUtils.h"
#include "Renderer/Device/ResourceManager.h"
#include "Renderer/Device/VulkanContext.h"
#include "Renderer/Features/Gi/GiCache.h"
#include "Renderer/Features/RayTracing/AccelerationStructures.h"

#include <span>
#include <stdexcept>

namespace batap
{
namespace
{
VkDescriptorSetLayout createFrameSetLayout(VkDevice device)
{
    std::array<VkDescriptorSetLayoutBinding, FrameSetBindingCount> bindings{};
    for (uint32_t i = 0; i < FrameSetBindingCount; ++i)
    {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = FrameSetBindingCount;
    layoutInfo.pBindings = bindings.data();

    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &layout) != VK_SUCCESS)
        throw std::runtime_error("FrameBindings : frame set layout");
    return layout;
}

VkDescriptorSetLayout createRayTracingSetLayout(VkDevice device)
{
    std::array<VkDescriptorSetLayoutBinding, 6> bindings{};
    bindings[0].binding = TlasBinding;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    bindings[1].binding = RtGeometriesBinding;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[2].binding = RtInstancesBinding;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[3].binding = RtCacheKeysBinding;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[4].binding = RtCacheValuesBinding;
    bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[5].binding = RtCacheStatsBinding;
    bindings[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    for (VkDescriptorSetLayoutBinding& binding : bindings)
    {
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &layout) != VK_SUCCESS)
        throw std::runtime_error("FrameBindings : ray tracing set layout");
    return layout;
}

VkDescriptorPool createPool(VkDevice device, std::span<const VkDescriptorPoolSize> sizes)
{
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = FramesInFlight;
    poolInfo.poolSizeCount = static_cast<uint32_t>(sizes.size());
    poolInfo.pPoolSizes = sizes.data();

    VkDescriptorPool pool = VK_NULL_HANDLE;
    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool) != VK_SUCCESS)
        throw std::runtime_error("FrameBindings : descriptor pool");
    return pool;
}

void allocateSets(VkDevice device, VkDescriptorPool pool, VkDescriptorSetLayout layout,
                  VkDescriptorSet* sets)
{
    std::array<VkDescriptorSetLayout, FramesInFlight> layouts;
    layouts.fill(layout);
    VkDescriptorSetAllocateInfo setInfo{};
    setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    setInfo.descriptorPool = pool;
    setInfo.descriptorSetCount = FramesInFlight;
    setInfo.pSetLayouts = layouts.data();
    if (vkAllocateDescriptorSets(device, &setInfo, sets) != VK_SUCCESS)
        throw std::runtime_error("FrameBindings : descriptor sets");
}

VkPipelineLayout createPipelineLayout(VkDevice device,
                                      std::span<const VkDescriptorSetLayout> setLayouts)
{
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.size = sizeof(DrawPush);

    VkPipelineLayoutCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    info.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
    info.pSetLayouts = setLayouts.data();
    info.pushConstantRangeCount = 1;
    info.pPushConstantRanges = &pushRange;

    VkPipelineLayout layout = VK_NULL_HANDLE;
    if (vkCreatePipelineLayout(device, &info, nullptr, &layout) != VK_SUCCESS)
        throw std::runtime_error("FrameBindings : pipeline layout");
    return layout;
}

void writeStorageBuffer(VkWriteDescriptorSet& write, VkDescriptorBufferInfo& info,
                        VkDescriptorSet set, uint32_t binding, VkBuffer buffer)
{
    info = {buffer, 0, VK_WHOLE_SIZE};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = set;
    write.dstBinding = binding;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.pBufferInfo = &info;
}
}  // namespace

FrameBindings::FrameBindings(VulkanContext& ctx, ResourceManager& resources)
    : ctx_(ctx), resources_(resources)
{
    const VkDevice device = ctx_.device_;
    frameSetLayout_ = createFrameSetLayout(device);
    const std::array<VkDescriptorPoolSize, 1> frameSizes{
        {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, FrameSetBindingCount * FramesInFlight}}};
    framePool_ = createPool(device, frameSizes);
    frameSets_.resize(FramesInFlight);
    allocateSets(device, framePool_, frameSetLayout_, frameSets_.data());
    layout_ =
        createPipelineLayout(device, std::array{resources_.textureSetLayout(), frameSetLayout_});

    if (!ctx_.rtSupported_)
        return;
    rtSetLayout_ = createRayTracingSetLayout(device);
    const std::array<VkDescriptorPoolSize, 2> rtSizes{{
        {VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, FramesInFlight},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 5 * FramesInFlight},
    }};
    rtPool_ = createPool(device, rtSizes);
    allocateSets(device, rtPool_, rtSetLayout_, rtSets_.data());
    rtLayout_ = createPipelineLayout(
        device, std::array{resources_.textureSetLayout(), frameSetLayout_, rtSetLayout_});
}

FrameBindings::~FrameBindings()
{
    vkDestroyPipelineLayout(ctx_.device_, rtLayout_, nullptr);
    vkDestroyPipelineLayout(ctx_.device_, layout_, nullptr);
    vkDestroyDescriptorPool(ctx_.device_, rtPool_, nullptr);
    vkDestroyDescriptorSetLayout(ctx_.device_, rtSetLayout_, nullptr);
    vkDestroyDescriptorPool(ctx_.device_, framePool_, nullptr);
    vkDestroyDescriptorSetLayout(ctx_.device_, frameSetLayout_, nullptr);
}

DrawSetup FrameBindings::drawSetup(VkFormat colorFormat, VkFormat depthFormat) const
{
    return DrawSetup{ctx_, resources_, layout_, colorFormat, depthFormat, rtLayout_};
}

DrawContext FrameBindings::drawContext(uint32_t frame, bool rayTracing) const
{
    DrawContext draw{};
    draw.layout_ = layout_;
    draw.rtLayout_ = rtLayout_;
    draw.sets_ = {resources_.textureSet(), frameSets_[frame]};
    if (rayTracing)
        draw.rtSet_ = rtSets_[frame];
    draw.rayTracing_ = rayTracing;
    return draw;
}

void FrameBindings::writeFrameSet(uint32_t frame,
                                  const std::array<VkBuffer, FrameSetBindingCount>& buffers)
{
    std::array<VkDescriptorBufferInfo, FrameSetBindingCount> bufferInfos{};
    std::array<VkWriteDescriptorSet, FrameSetBindingCount> writes{};
    for (uint32_t i = 0; i < FrameSetBindingCount; ++i)
    {
        if (buffers[i] == VK_NULL_HANDLE)
            ThrowRuntime("frame set: a binding has no buffer behind it");
        writeStorageBuffer(writes[i], bufferInfos[i], frameSets_[frame], i, buffers[i]);
    }
    vkUpdateDescriptorSets(ctx_.device_, FrameSetBindingCount, writes.data(), 0, nullptr);
}

void FrameBindings::writeRayTracingSet(uint32_t frame, const AccelerationStructures& structures,
                                       const GiCache& cache)
{
    const VkDescriptorSet set = rtSets_[frame];
    const VkAccelerationStructureKHR tlas = structures.tlas(frame);
    VkWriteDescriptorSetAccelerationStructureKHR tlasWrite{};
    tlasWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
    tlasWrite.accelerationStructureCount = 1;
    tlasWrite.pAccelerationStructures = &tlas;
    std::array<VkWriteDescriptorSet, 6> writes{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].pNext = &tlasWrite;
    writes[0].dstSet = set;
    writes[0].dstBinding = TlasBinding;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    std::array<VkDescriptorBufferInfo, 5> bufferInfos{};
    writeStorageBuffer(writes[1], bufferInfos[0], set, RtGeometriesBinding,
                       resources_.bufferFor(structures.hitGeometries(frame)));
    writeStorageBuffer(writes[2], bufferInfos[1], set, RtInstancesBinding,
                       resources_.bufferFor(structures.hitInstances(frame)));
    writeStorageBuffer(writes[3], bufferInfos[2], set, RtCacheKeysBinding,
                       resources_.bufferFor(cache.keys()));
    writeStorageBuffer(writes[4], bufferInfos[3], set, RtCacheValuesBinding,
                       resources_.bufferFor(cache.values()));
    writeStorageBuffer(writes[5], bufferInfos[4], set, RtCacheStatsBinding,
                       resources_.bufferFor(cache.stats()));
    vkUpdateDescriptorSets(ctx_.device_, static_cast<uint32_t>(writes.size()), writes.data(), 0,
                           nullptr);
}
}  // namespace batap
