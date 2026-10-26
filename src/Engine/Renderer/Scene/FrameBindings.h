#pragma once

#include <volk.h>

#include "Renderer/Draws/DrawContext.h"
#include "Renderer/EngineConfig.h"
#include "Shaders/ShaderInterop.h"

#include <array>
#include <cstdint>
#include <vector>

namespace batap
{
struct VulkanContext;
struct ResourceManager;
struct AccelerationStructures;
struct GiCache;

struct FrameBindings
{
    FrameBindings(VulkanContext& ctx, ResourceManager& resources);
    ~FrameBindings();

    FrameBindings(const FrameBindings&) = delete;
    FrameBindings& operator=(const FrameBindings&) = delete;

    DrawSetup drawSetup(VkFormat colorFormat, VkFormat depthFormat) const;
    DrawContext drawContext(uint32_t frame, bool rayTracing) const;

    void writeFrameSet(uint32_t frame, const std::array<VkBuffer, FrameSetBindingCount>& buffers);
    void writeRayTracingSet(uint32_t frame, const AccelerationStructures& structures,
                            const GiCache& cache);

   private:
    VulkanContext& ctx_;
    ResourceManager& resources_;
    VkDescriptorSetLayout frameSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool framePool_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> frameSets_;
    VkDescriptorSetLayout rtSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool rtPool_ = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, FramesInFlight> rtSets_{};
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipelineLayout rtLayout_ = VK_NULL_HANDLE;
};
}  // namespace batap
