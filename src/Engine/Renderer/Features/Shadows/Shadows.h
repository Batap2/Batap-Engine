#pragma once

#include "EigenTypes.h"
#include "Handles.h"
#include "Renderer/Device/ShaderLibrary.h"
#include "Renderer/Draws/DrawContext.h"
#include "Renderer/Features/Shadows/LocalShadowAllocator.h"
#include "Renderer/Graph/RenderGraph.h"

#include "entt/entt.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace batap
{
struct GPUInstanceManager;
struct TerrainDrawer;

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

struct Shadows
{
    explicit Shadows(const DrawSetup& setup);
    ~Shadows();

    Shadows(const Shadows&) = delete;
    Shadows& operator=(const Shadows&) = delete;

    struct Output
    {
        std::array<GraphImage, 2> atlases_;
        GraphBuffer shadows_;
        bool hasEntries_ = false;
    };

    void buildPipelines(const ShaderModules& modules);
    void prepare(entt::registry& reg, GPUInstanceManager& instances, entt::entity camera,
                 v2i frameSize, bool rayTracing);
    Output addPasses(RenderGraph& graph, GraphBuffer uploads, const DrawContext& draw,
                     TerrainDrawer& terrain);

    GPUResourceHandle buffer() const { return buffer_; }

   private:
    struct AtlasViews
    {
        const char* name_ = "";
        GraphImage atlas_;
        uint32_t size_ = 0;
        std::span<const ShadowView> views_;
    };

    void reserve(size_t entryCount);
    void fitCascades(entt::registry& reg, GPUInstanceManager& instances, entt::entity camera,
                     v2i frameSize);

    DrawSetup setup_;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    GPUResourceHandle buffer_;
    size_t capacity_ = 0;
    size_t entryCount_ = 0;
    std::vector<ShadowGPUData> entries_;
    GPUResourceHandle localAtlas_;
    GPUResourceHandle cascadeAtlas_;
    LocalShadowAllocator localAllocator_;
    std::vector<ShadowView> localViews_;
    std::vector<ShadowView> cascadeViews_;
    uint32_t warnedCascades_ = 0;
};
}  // namespace batap
