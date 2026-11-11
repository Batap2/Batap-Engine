#pragma once

#include "Handles.h"
#include "Renderer/Device/ShaderLibrary.h"
#include "Renderer/Draws/DrawContext.h"
#include "Renderer/ScatterDraws.h"
#include "Shaders/ShaderInterop.h"

#include <array>
#include <cstdint>
#include <map>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

namespace batap
{

// Camera-relative world axes. A cascade radius of 0 culls nothing, a cone of 0
// culls nothing against the view.
struct ScatterShadowView
{
    v4f cascade_{v4f::Zero()};
    v3f lightDir_ = v3f::Zero();
    std::span<const v4f> finerCascades_;
    v3f viewForward_ = v3f::Zero();
    float viewCone_ = 0.f;
};

struct ScatterDrawer
{
    explicit ScatterDrawer(const DrawSetup& setup);
    ~ScatterDrawer();

    ScatterDrawer(const ScatterDrawer&) = delete;
    ScatterDrawer& operator=(const ScatterDrawer&) = delete;

    void buildPipelines(const ShaderModules& modules);
    void upload(ScatterDraws& draws);
    void recordPrepass(const DrawContext& draw);
    void record(const DrawContext& draw);
    void recordShadow(const DrawContext& draw, const ScatterShadowView& view);
    bool hasShadowCasters() const;

    GPUResourceHandle batchBuffer() const { return batches_; }
    GPUResourceHandle instanceBuffer() const { return instances_; }

   private:
    struct Slot
    {
        MeshHandle mesh_;
        MeshHandle shadowMesh_;
        std::array<uint32_t, 8> materials_{};
        uint64_t first_ = 0;
        uint64_t count_ = 0;
        bool castShadows_ = true;
        float shadowLength_ = 0.f;
        float fadeStart_ = 0.f;
        float fadeEnd_ = 0.f;
        float impostorRadius_ = 0.f;
        m4f placedWorld_ = m4f::Identity();
        uint64_t placedFrame_ = 0;
    };

    struct Draw
    {
        MeshHandle mesh_;
        MeshHandle shadowMesh_;
        v4f bounds_{v4f::Zero()};
        float shadowLength_ = 0.f;
        uint32_t batch_ = 0;
        uint32_t count_ = 0;
        bool castShadows_ = true;
        bool visible_ = true;
        bool impostor_ = false;
    };

    void reserve(uint64_t instances);
    uint64_t allocate(uint64_t count);
    void release(uint64_t first, uint64_t count);
    static bool castsShadow(const Draw& d, const ScatterShadowView& view);
    void recordDraws(const DrawContext& draw, VkPipeline pipeline, uint32_t streams,
                     const ScatterShadowView* shadow) const;
    void recordImpostors(const DrawContext& draw, VkPipeline pipeline) const;

    DrawSetup setup_;
    VkPipeline prepassPipeline_ = VK_NULL_HANDLE;
    VkPipeline reflectPrepassPipeline_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipeline rtPipeline_ = VK_NULL_HANDLE;
    VkPipeline shadowPipeline_ = VK_NULL_HANDLE;
    VkPipeline impostorPrepassPipeline_ = VK_NULL_HANDLE;
    VkPipeline impostorReflectPrepassPipeline_ = VK_NULL_HANDLE;
    VkPipeline impostorPipeline_ = VK_NULL_HANDLE;
    VkPipeline impostorRtPipeline_ = VK_NULL_HANDLE;
    GPUResourceHandle batches_;
    GPUResourceHandle instances_;
    uint64_t capacity_ = 0;
    uint64_t end_ = 0;
    std::map<uint64_t, uint64_t> free_;
    std::unordered_map<uint64_t, Slot> slots_;
    std::vector<std::pair<uint64_t, Slot>> retiredSlots_;
    std::vector<std::pair<uint64_t, GPUResourceHandle>> retiredBuffers_;
    uint64_t frame_ = 0;
    std::vector<ScatterBatchGPUData> batchData_;
    std::vector<Draw> draws_;
    bool warnedFull_ = false;
};

}  // namespace batap
