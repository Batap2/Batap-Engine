#pragma once

#include "Handles.h"
#include "Renderer/Device/ShaderLibrary.h"
#include "Renderer/Draws/DrawContext.h"
#include "Renderer/Graph/RenderGraph.h"
#include "Renderer/ScatterDraws.h"
#include "Shaders/ShaderInterop.h"

#include <array>
#include <cstdint>
#include <map>
#include <optional>
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
    uint32_t cullView_ = 0;
};

struct AssetManager;

struct ScatterCullBuffers
{
    GraphBuffer commands_;
    GraphBuffer visible_;
    GraphBuffer table_;
    GraphBuffer cullViews_;
    GraphBuffer visibility_;
    bool hiz_ = false;
};

struct ScatterDrawer
{
    explicit ScatterDrawer(const DrawSetup& setup);
    ~ScatterDrawer();

    ScatterDrawer(const ScatterDrawer&) = delete;
    ScatterDrawer& operator=(const ScatterDrawer&) = delete;

    void buildPipelines(const ShaderModules& modules);
    void upload(ScatterDraws& draws, AssetManager& assets);
    void setGpuCulling(bool on) { gpuCulling_ = on; }
    void prepareHzb(VkExtent2D extent);
    std::optional<ScatterCullBuffers> addCullPass(RenderGraph& graph, const DrawContext& draw,
                                                  GraphBuffer uploads,
                                                  const ScatterCullGPUData& views);
    bool addOcclusionPasses(RenderGraph& graph, const DrawContext& draw,
                            const ScatterCullBuffers& culled, GraphImage depth,
                            GraphBuffer uploads);
    void recordPrepass(const DrawContext& draw);
    void recordPrepassLate(const DrawContext& draw);
    void record(const DrawContext& draw);
    void recordShadow(const DrawContext& draw, const ScatterShadowView& view);
    bool hasShadowCasters() const;

    GPUResourceHandle batchBuffer() const { return batches_; }
    GPUResourceHandle instanceBuffer() const { return instances_; }
    GPUResourceHandle ownerBuffer() const { return owners_; }
    GPUResourceHandle instanceBatchBuffer() const { return instanceBatches_; }
    GPUResourceHandle drawGroupBuffer() const { return groupBuffer_; }
    GPUResourceHandle drawCommandBuffer() const { return commands_; }
    GPUResourceHandle visibleBuffer() const { return visible_; }
    GPUResourceHandle hzbBuffer() const { return hzb_; }
    GPUResourceHandle visibilityBuffer() const { return visibility_; }
    GPUResourceHandle cullBuffer() const { return cullData_; }

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
        float radius_ = 0.f;
        uint32_t batch_ = InvalidGPUIndex;
        uint32_t drawGroup_ = 0;
        uint32_t drawGroups_ = 0;
        uint32_t shadowGroup_ = 0;
        uint32_t shadowGroups_ = 0;
        bool written_ = false;
    };

    struct Group
    {
        MeshHandle mesh_;
        uint32_t submesh_ = 0;
        uint64_t instances_ = 0;
        uint64_t shadowInstances_ = 0;
    };

    struct OwnerSlot
    {
        uint32_t index_ = 0;
        uint64_t frame_ = 0;
        ScatterDraws::Owner last_;
    };

    struct Draw
    {
        MeshHandle mesh_;
        MeshHandle shadowMesh_;
        v4f bounds_{v4f::Zero()};
        float shadowLength_ = 0.f;
        uint32_t first_ = 0;
        uint32_t count_ = 0;
        bool castShadows_ = true;
        bool visible_ = true;
        bool impostor_ = false;
    };

    void reserve(uint64_t instances);
    uint64_t allocate(uint64_t count);
    void release(uint64_t first, uint64_t count);
    bool writeBatch(Slot& slot, const ScatterDraws::Placement& placement, uint32_t owner,
                    AssetManager& assets);
    void writeEmpty(uint32_t batch);
    void flushBatchWrites();
    std::pair<uint32_t, uint32_t> groupsOf(MeshHandle mesh, AssetManager& assets);
    void countGroups(const Slot& slot, int32_t delta);
    void reserveVisible();
    void recordIndirect(const DrawContext& draw, VkPipeline meshPipeline, VkPipeline impostorPipeline,
                        uint32_t streams, uint32_t view) const;
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
    VkPipeline cullPipeline_ = VK_NULL_HANDLE;
    VkPipeline lateCullPipeline_ = VK_NULL_HANDLE;
    VkPipeline hzbPipeline_ = VK_NULL_HANDLE;
    GPUResourceHandle batches_;
    GPUResourceHandle groupBuffer_;
    GPUResourceHandle commands_;
    GPUResourceHandle visible_;
    uint64_t visibleCapacity_ = 0;
    GPUResourceHandle visibility_;
    GPUResourceHandle hzb_;
    uint64_t hzbCapacity_ = 0;
    uint32_t hzbLevels_ = 0;
    VkExtent2D extent_{};
    bool hiz_ = true;
    bool hizActive_ = false;
    uint32_t mainStride_ = 0;
    GPUResourceHandle quadIndices_;
    GPUResourceHandle cullData_;
    uint32_t shadowStride_ = 0;
    std::vector<Group> groups_;
    std::unordered_map<uint64_t, uint32_t> groupOf_;
    std::vector<ScatterDrawGroupGPUData> groupData_;
    bool gpuCulling_ = false;
    bool warnedGroups_ = false;
    GPUResourceHandle instances_;
    GPUResourceHandle instanceBatches_;
    GPUResourceHandle owners_;
    std::vector<uint32_t> freeBatches_;
    uint32_t nextBatch_ = 0;
    std::unordered_map<uint32_t, OwnerSlot> ownerSlots_;
    std::vector<ScatterOwnerGPUData> ownerData_;
    uint64_t capacity_ = 0;
    uint64_t end_ = 0;
    std::map<uint64_t, uint64_t> free_;
    std::unordered_map<uint64_t, Slot> slots_;
    std::vector<std::pair<uint64_t, Slot>> retiredSlots_;
    std::vector<std::pair<uint64_t, GPUResourceHandle>> retiredBuffers_;
    uint64_t frame_ = 0;
    std::vector<Draw> draws_;
    std::vector<std::pair<uint64_t, ScatterDraws::Placement>> pending_;
    // Uploads within a frame are unordered: one copy per batch, the last write.
    std::unordered_map<uint32_t, ScatterBatchGPUData> batchWrites_;
    bool warnedFull_ = false;
};

}  // namespace batap
