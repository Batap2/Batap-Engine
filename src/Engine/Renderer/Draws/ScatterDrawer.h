#pragma once

#include "Handles.h"
#include "Renderer/Device/ShaderLibrary.h"
#include "Renderer/Draws/DrawContext.h"
#include "Renderer/ScatterDraws.h"
#include "Shaders/ShaderInterop.h"

#include <array>
#include <cstdint>
#include <map>
#include <unordered_map>
#include <utility>
#include <vector>

namespace batap
{

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
    void recordShadow(const DrawContext& draw);

    GPUResourceHandle batchBuffer() const { return batches_; }
    GPUResourceHandle instanceBuffer() const { return instances_; }

   private:
    struct Slot
    {
        MeshHandle mesh_;
        std::array<uint32_t, 8> materials_{};
        uint64_t first_ = 0;
        uint64_t count_ = 0;
        bool castShadows_ = true;
        float fadeStart_ = 0.f;
        float fadeEnd_ = 0.f;
        m4f placedWorld_ = m4f::Identity();
        uint64_t placedFrame_ = 0;
    };

    struct Draw
    {
        MeshHandle mesh_;
        uint32_t batch_ = 0;
        uint32_t count_ = 0;
        bool castShadows_ = true;
        bool visible_ = true;
    };

    void reserve(uint64_t instances);
    uint64_t allocate(uint64_t count);
    void release(uint64_t first, uint64_t count);
    void recordDraws(const DrawContext& draw, VkPipeline pipeline, uint32_t streams,
                     bool castersOnly) const;

    DrawSetup setup_;
    VkPipeline prepassPipeline_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipeline rtPipeline_ = VK_NULL_HANDLE;
    VkPipeline shadowPipeline_ = VK_NULL_HANDLE;
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
