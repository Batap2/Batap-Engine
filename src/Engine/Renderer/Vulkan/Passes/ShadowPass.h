#pragma once

#include "EigenTypes.h"
#include "Handles.h"
#include "Renderer/Vulkan/Passes/Pass.h"
#include "Renderer/Vulkan/Passes/ShaderCatalog.h"
#include "Renderer/Vulkan/RenderGraph.h"

#include <span>
#include <vector>

namespace batap
{
struct TerrainPass;

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
    const char* name_ = "";
    GraphImage atlas_;
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

    void addPasses(RenderGraph& graph, GraphBuffer shadows, const PassContext& pass,
                   std::span<const ShadowAtlasViews> atlases, size_t entryCount,
                   TerrainPass& terrain);

    GPUResourceHandle buffer() const { return buffer_; }

   private:
    PassSetup setup_;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    GPUResourceHandle buffer_;
    size_t capacity_ = 0;
    std::vector<ShadowGPUData> entries_;
};
}  // namespace batap
