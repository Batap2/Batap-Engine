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

#include <cassert>
#include <cstdint>

namespace batap
{

// How a resource is about to be used, or was last used.
enum class Usage : uint8_t
{
    None,                        // nothing to wait for, contents undefined
    TransferSrc,                 // source of a copy / blit
    TransferDst,                 // destination of a copy / blit / clear
    ShaderRead,                  // sampled or read by any shader stage
    ShaderWrite,                 // storage writes and atomics from a fragment shader
    ColorAttachment,             // render target
    DepthAttachment,             // depth test / write
    Present,                     // handed to the compositor
    AnyRead,                     // read by anything, anywhere — global barriers only
    AccelerationStructureBuild,  // written or read by an acceleration structure build
    AccelerationStructureRead,
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
        case Usage::ShaderWrite:
            return {VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                    VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                    VK_IMAGE_LAYOUT_GENERAL};
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
        case Usage::AccelerationStructureBuild:
            return {VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                    VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR |
                        VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
                    VK_IMAGE_LAYOUT_UNDEFINED};
        case Usage::AccelerationStructureRead:
            return {VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
                        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR, VK_IMAGE_LAYOUT_UNDEFINED};
    }

    // Fallback outside the switch: no default label, so adding a Usage without
    // mapping it here is a compile error rather than a silent runtime guess.
    assert(false && "usageInfo: unmapped usage");
    return {VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL};
}
}  // namespace batap
