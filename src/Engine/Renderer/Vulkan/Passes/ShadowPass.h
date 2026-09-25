#pragma once

#include "EigenTypes.h"
#include "Handles.h"
#include "Renderer/Vulkan/Passes/Pass.h"
#include "Renderer/Vulkan/Passes/ShaderCatalog.h"

#include <span>
#include <vector>

namespace batap
{
struct ShadowView
{
    m4f viewProj_;
    float texelWorld_ = 0.f;
    uint32_t x_ = 0;
    uint32_t y_ = 0;
    uint32_t size_ = 0;
    uint32_t entry_ = 0;
    float strength_ = 1.f;
};

struct ShadowPass
{
    explicit ShadowPass(const PassSetup& setup);
    ~ShadowPass();

    ShadowPass(const ShadowPass&) = delete;
    ShadowPass& operator=(const ShadowPass&) = delete;

    void buildPipelines(const ShaderModules& modules);

    // Before the frame set is written: growing replaces the buffer it binds.
    void reserve(size_t entryCount);

    // Opens its own rendering scope: it writes the atlas the scene pass reads,
    // so it cannot sit inside the scene's. Every entry up to entryCount is
    // rewritten, those no view claims with a zero strength.
    void record(const PassContext& pass, GPUResourceHandle atlas,
                std::span<const ShadowView> views, size_t entryCount);

    GPUResourceHandle buffer() const { return buffer_; }

   private:
    PassSetup setup_;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    GPUResourceHandle buffer_;
    size_t capacity_ = 0;
    std::vector<ShadowGPUData> entries_;
};
}  // namespace batap
