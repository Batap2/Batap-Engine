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
    v4f sphere_{v4f::Zero()};
    float texelWorld_ = 0.f;
    uint32_t x_ = 0;
    uint32_t y_ = 0;
    uint32_t size_ = 0;
    uint32_t entry_ = 0;
    float strength_ = 1.f;
};

struct ShadowAtlasViews
{
    GPUResourceHandle atlas_;
    uint32_t size_ = 0;
    std::span<const ShadowView> views_;
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

    // Opens its own rendering scopes, one per atlas: it writes the atlases the
    // scene pass reads, so it cannot sit inside the scene's. Every entry up to
    // entryCount is rewritten, those no view claims with a zero strength.
    void record(const PassContext& pass, std::span<const ShadowAtlasViews> atlases,
                size_t entryCount);

    GPUResourceHandle buffer() const { return buffer_; }

   private:
    void recordAtlas(const PassContext& pass, const ShadowAtlasViews& atlas);

    PassSetup setup_;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    GPUResourceHandle buffer_;
    size_t capacity_ = 0;
    std::vector<ShadowGPUData> entries_;
};
}  // namespace batap
