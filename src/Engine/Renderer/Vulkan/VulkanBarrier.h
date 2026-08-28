#pragma once

// Barriers, named by intent instead of by their eleven fields. A Vulkan barrier
// carries three independent axes — pipeline stage (when), memory access (which
// caches), image layout (how the texels are arranged) — and every call site has
// to spell out all three. The Usage enum below collapses them into one name per
// way a resource can be used, which is the D3D12_RESOURCE_STATES model rebuilt
// on top of synchronization2.
//
// The usage table is deliberately a little wider than each individual call site
// needs (ALL_TRANSFER rather than COPY vs BLIT, READ|WRITE on attachments).
// Widening a barrier is always correct — it waits for more and blocks more — and
// these all sit on per-frame or upload paths where the lost overlap is noise.
// Narrowing would be the dangerous direction, so the table never does it.

#include <volk.h>

#include <array>
#include <cassert>
#include <cstdint>

namespace batap
{

// How a resource is about to be used, or was last used.
enum class Usage : uint8_t
{
    None,             // nothing to wait for, contents undefined
    TransferSrc,      // source of a copy / blit
    TransferDst,      // destination of a copy / blit / clear
    ShaderRead,       // sampled or read by any shader stage
    ColorAttachment,  // render target
    DepthAttachment,  // depth test / write
    Present,          // handed to the compositor
    AnyRead,          // read by anything, anywhere — global barriers only
};

// Keeps the source usage's stage and access but forces oldLayout to UNDEFINED:
// wait for the previous writes to finish, then let the driver drop the contents
// instead of preserving (and possibly decompressing) them.
enum class Discard : uint8_t
{
    No,
    Yes,
};

struct UsageInfo
{
    VkPipelineStageFlags2 stage;
    VkAccessFlags2 access;
    VkImageLayout layout;
};

inline UsageInfo usageInfo(Usage usage)
{
    switch (usage)
    {
        case Usage::None:
            return {VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0, VK_IMAGE_LAYOUT_UNDEFINED};
        case Usage::TransferSrc:
            return {VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL};
        case Usage::TransferDst:
            return {VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL};
        case Usage::ShaderRead:
            return {VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_SHADER_READ_BIT,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        case Usage::ColorAttachment:
            return {VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        case Usage::DepthAttachment:
            return {VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                        VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                    VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                    VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL};
        case Usage::Present:
            // The present engine is synchronised by the semaphore, not by an
            // access mask — BOTTOM_OF_PIPE with no access is the canonical form.
            return {VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, 0, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR};
        case Usage::AnyRead:
            return {VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT,
                    VK_IMAGE_LAYOUT_UNDEFINED};
    }

    // Fallback outside the switch: no default label, so adding a Usage without
    // mapping it here is a compile error rather than a silent runtime guess.
    assert(false && "usageInfo: unmapped usage");
    return {VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
            VK_IMAGE_LAYOUT_GENERAL};
}

inline VkImageSubresourceRange colorRange(uint32_t baseMip = 0, uint32_t mipCount = 1,
                                          uint32_t baseLayer = 0, uint32_t layerCount = 1)
{
    return {VK_IMAGE_ASPECT_COLOR_BIT, baseMip, mipCount, baseLayer, layerCount};
}

inline VkImageSubresourceRange depthRange(uint32_t baseMip = 0, uint32_t mipCount = 1,
                                          uint32_t baseLayer = 0, uint32_t layerCount = 1)
{
    return {VK_IMAGE_ASPECT_DEPTH_BIT, baseMip, mipCount, baseLayer, layerCount};
}

// Accumulates the barriers of one synchronisation point, then emits them in a
// single vkCmdPipelineBarrier2. Batching is not just for tidiness: barriers that
// belong to the same point should travel together, so the driver sees one
// dependency instead of a chain of them.
class BarrierBatch
{
public:
    BarrierBatch& image(VkImage img, Usage from, Usage to, VkImageSubresourceRange range,
                        Discard discard = Discard::No)
    {
        assert(from != Usage::AnyRead && to != Usage::AnyRead &&
               "BarrierBatch::image: AnyRead has no layout, use memory()");
        assert(imageCount_ < Capacity && "BarrierBatch: raise Capacity");
        if (imageCount_ >= Capacity)
            return *this;

        const UsageInfo src = usageInfo(from);
        const UsageInfo dst = usageInfo(to);

        VkImageMemoryBarrier2& b = images_[imageCount_++];
        b = {};
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        b.srcStageMask = src.stage;
        b.srcAccessMask = src.access;
        b.dstStageMask = dst.stage;
        b.dstAccessMask = dst.access;
        b.oldLayout = discard == Discard::Yes ? VK_IMAGE_LAYOUT_UNDEFINED : src.layout;
        b.newLayout = dst.layout;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = img;
        b.subresourceRange = range;
        return *this;
    }

    // Global memory dependency, for buffers. Drivers largely ignore the offset
    // and size of a VkBufferMemoryBarrier2 — caches flush wholesale — so a
    // per-buffer barrier would cost the same as this one. The only reason to
    // name a buffer is a queue family ownership transfer, which we never do.
    BarrierBatch& memory(Usage from, Usage to)
    {
        const UsageInfo src = usageInfo(from);
        const UsageInfo dst = usageInfo(to);

        memory_ = {};
        memory_.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
        memory_.srcStageMask = src.stage;
        memory_.srcAccessMask = src.access;
        memory_.dstStageMask = dst.stage;
        memory_.dstAccessMask = dst.access;
        hasMemory_ = true;
        return *this;
    }

    void flush(VkCommandBuffer cmd)
    {
        if (imageCount_ == 0 && !hasMemory_)
            return;

        VkDependencyInfo dep{};
        dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep.memoryBarrierCount = hasMemory_ ? 1u : 0u;
        dep.pMemoryBarriers = hasMemory_ ? &memory_ : nullptr;
        dep.imageMemoryBarrierCount = imageCount_;
        dep.pImageMemoryBarriers = imageCount_ != 0 ? images_.data() : nullptr;
        vkCmdPipelineBarrier2(cmd, &dep);

        imageCount_ = 0;
        hasMemory_ = false;
    }

private:
    static constexpr uint32_t Capacity = 8;

    std::array<VkImageMemoryBarrier2, Capacity> images_{};
    VkMemoryBarrier2 memory_{};
    uint32_t imageCount_ = 0;
    bool hasMemory_ = false;
};
}  // namespace batap
