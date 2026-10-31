#pragma once

#include <volk.h>

#include "Assets/Mesh.h"
#include "EigenTypes.h"
#include "Handles.h"
#include "Renderer/EngineConfig.h"
#include "Renderer/Graph/RenderGraph.h"
#include "Shaders/ShaderInterop.h"

#include <imgui.h>

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace batap
{
struct VulkanContext;
struct ResourceManager;

struct ThumbnailDraw
{
    Mesh mesh_;
    m4f world_ = m4f::Identity();
    std::vector<Material> materials_;
};

struct Thumbnails
{
    using Id = uint32_t;
    static constexpr Id None = UINT32_MAX;
    static constexpr uint32_t TileSize = 128;
    static constexpr uint32_t AtlasSize = 2048;
    static constexpr uint32_t TilesPerRow = AtlasSize / TileSize;
    static constexpr uint32_t TileCount = TilesPerRow * TilesPerRow;
    static constexpr uint32_t MaxDraws = 256;
    static constexpr uint32_t MaxMaterials = 1024;

    Thumbnails(VulkanContext& ctx, ResourceManager& resources, VkFormat depthFormat);
    ~Thumbnails();

    Thumbnails(const Thumbnails&) = delete;
    Thumbnails& operator=(const Thumbnails&) = delete;

    Id addScene(std::vector<ThumbnailDraw> draws);
    Id addMaterial(const Material& material);
    Id addImage(uint32_t textureIndex, uint32_t width, uint32_t height, bool hdr);
    void remove(Id id);
    bool ready(Id id) const;
    GPUResourceHandle atlas() const { return atlas_; }
    ImVec2 uv0(Id id) const;
    ImVec2 uv1(Id id) const;

    std::optional<GraphImage> addPasses(RenderGraph& graph, uint32_t frame, GraphBuffer uploads);

   private:
    struct Batch
    {
        Mesh mesh_;
        ThumbnailDrawGPUData data_{};
        std::vector<Material> materials_;
    };

    struct Tile
    {
        bool used_ = false;
        bool pending_ = false;
        bool ready_ = false;
        uint32_t generation_ = 1;
        std::vector<Batch> draws_;
    };

    struct Work
    {
        uint32_t tile_ = 0;
        uint32_t first_ = 0;
        uint32_t count_ = 0;
    };

    struct Draw
    {
        Mesh mesh_;
        uint32_t index_ = 0;
    };

    static constexpr uint32_t kTilesPerFrame = 8;

    Id allocate();
    Id addView(std::vector<ThumbnailDraw> draws, const v3f& centre, float boundingRadius);
    Tile* find(Id id);
    const Tile* find(Id id) const;
    void writeSet(uint32_t frame);
    void record(VkCommandBuffer cmd, uint32_t frame, const std::vector<Work>& work,
                const std::vector<Draw>& draws);
    Mesh uploadMesh(const std::vector<v3f>& positions, const std::vector<v3f>& normals,
                    const std::vector<v2f>& uvs, const std::vector<v4f>& tangents,
                    const std::vector<uint32_t>& indices, const char* name);
    Mesh buildSphere();
    Mesh buildQuad();

    VulkanContext& ctx_;
    ResourceManager& resources_;
    VkFormat depthFormat_ = VK_FORMAT_UNDEFINED;
    GPUResourceHandle atlas_;
    GPUResourceHandle draws_;
    GPUResourceHandle materials_;
    Mesh sphere_;
    Mesh quad_;
    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, FramesInFlight> sets_{};
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    std::array<Tile, TileCount> tiles_{};
    bool cleared_ = false;
};
}  // namespace batap
