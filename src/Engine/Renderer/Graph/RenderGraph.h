#pragma once

#include "Handles.h"
#include "Renderer/Device/Usage.h"

#include <volk.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace batap
{
struct ResourceManager;

struct GraphImage
{
    uint32_t index_ = UINT32_MAX;
};

struct GraphBuffer
{
    uint32_t index_ = UINT32_MAX;
};

enum class GraphQueue : uint8_t
{
    Graphics,
    Compute,
    Transfer,
};
inline constexpr size_t GraphQueueCount = 3;

enum class Load : uint8_t
{
    Keep,
    Clear,
    DontCare,
};

struct GraphImageInfo
{
    VkImage image_ = VK_NULL_HANDLE;
    VkImageView view_ = VK_NULL_HANDLE;
    VkExtent2D extent_{};
    VkImageAspectFlags aspect_ = VK_IMAGE_ASPECT_COLOR_BIT;
    uint32_t textureIndex_ = UINT32_MAX;
    GPUResourceHandle resource_{};
};

struct GraphImageDesc
{
    VkExtent2D extent_{};
    VkFormat format_ = VK_FORMAT_UNDEFINED;
};

struct GraphAllocator
{
    std::function<GraphImageInfo(const std::string& name, const GraphImageDesc& desc,
                                 VkImageUsageFlags usage)>
        create_;
    std::function<void(const GraphImageInfo& image)> destroy_;
};

struct GraphAccess
{
    uint32_t resource_ = 0;
    bool image_ = false;
    Usage usage_ = Usage::None;
    bool reads_ = false;
    bool writes_ = false;
    bool discards_ = false;
};

struct GraphAttachment
{
    GraphImage image_;
    Load load_ = Load::Keep;
    VkClearValue clear_{};
    VkAttachmentStoreOp store_ = VK_ATTACHMENT_STORE_OP_STORE;
};

struct GraphBarrier
{
    uint32_t resource_ = 0;
    bool image_ = false;
    Usage from_ = Usage::None;
    Usage to_ = Usage::None;
    bool discard_ = false;
    bool afterWait_ = false;
};

struct GraphBatch
{
    uint32_t queue_ = 0;
    uint32_t firstPass_ = 0;
    uint32_t passCount_ = 0;
    std::vector<uint32_t> waits_;
    bool signals_ = false;
    std::vector<GraphBarrier> exits_;
};

struct GraphContext;

struct GraphPass
{
    GraphPass& read(GraphImage handle, Usage usage);
    GraphPass& write(GraphImage handle, Usage usage);
    GraphPass& read(GraphBuffer handle, Usage usage);
    GraphPass& write(GraphBuffer handle, Usage usage);
    GraphPass& color(GraphImage handle, Load load, VkClearColorValue clear = {});
    GraphPass& depth(GraphImage handle, Load load, float clear = 0.f);
    GraphPass& execute(std::function<void(GraphContext&)> fn);

    std::string name_;
    GraphQueue queue_ = GraphQueue::Graphics;
    std::vector<GraphAccess> accesses_;
    std::vector<GraphAttachment> colors_;
    std::optional<GraphAttachment> depth_;
    std::function<void(GraphContext&)> execute_;
    std::vector<GraphBarrier> barriers_;
    uint32_t batch_ = 0;
};

struct RenderGraph
{
    explicit RenderGraph(std::array<uint32_t, GraphQueueCount> queues = {});

    GraphImage importImage(std::string name, const GraphImageInfo& info, Usage in, Usage out);
    GraphImage importImage(std::string name, ResourceManager& resources, GPUResourceHandle image,
                           Usage in, Usage out);
    GraphImage importImage(std::string name, const GraphImageInfo& info, Usage out);
    void forget(VkImage image);
    void setAllocator(GraphAllocator allocator);
    GraphImage createImage(std::string name, const GraphImageDesc& desc);
    GraphBuffer importBuffer(std::string name, VkBuffer buffer, Usage in, Usage out);
    GraphBuffer importBuffer(std::string name, ResourceManager& resources, GPUResourceHandle buffer,
                             Usage in, Usage out);

    GraphPass& addPass(std::string name, GraphQueue queue = GraphQueue::Graphics);

    void reset();
    void compile();
    void record(uint32_t batch, VkCommandBuffer cmd);

    uint32_t textureIndex(GraphImage handle) const;
    uint32_t firstBatch(GraphImage handle) const;
    uint32_t lastBatch(GraphImage handle) const;

    const std::deque<GraphPass>& passes() const { return passes_; }
    const std::vector<GraphBatch>& batches() const { return batches_; }

   private:
    friend struct GraphContext;

    struct ImageResource
    {
        std::string name_;
        GraphImageInfo info_;
        Usage in_ = Usage::None;
        Usage out_ = Usage::None;
        bool kept_ = false;
        bool transient_ = false;
        GraphImageDesc desc_{};
        uint32_t physical_ = UINT32_MAX;
    };

    struct Transient
    {
        GraphImageDesc desc_;
        VkImageUsageFlags usage_ = 0;
        GraphImageInfo info_;
        Usage last_ = Usage::None;
        uint64_t frame_ = 0;
    };

    struct BufferResource
    {
        std::string name_;
        VkBuffer buffer_ = VK_NULL_HANDLE;
        Usage in_ = Usage::None;
        Usage out_ = Usage::None;
    };

    const std::string& resourceName(const GraphAccess& access) const;
    void allocateTransients();
    void recordBarriers(VkCommandBuffer cmd, const std::vector<GraphBarrier>& barriers) const;
    void recordPass(GraphPass& pass, VkCommandBuffer cmd);

    std::array<uint32_t, GraphQueueCount> queues_{};
    std::vector<ImageResource> images_;
    std::vector<BufferResource> buffers_;
    std::deque<GraphPass> passes_;
    std::vector<GraphBatch> batches_;
    bool compiled_ = false;
    std::unordered_map<VkImage, Usage> keptUsages_;
    GraphAllocator allocator_;
    std::vector<Transient> transients_;
    uint64_t frame_ = 0;
};

struct GraphContext
{
    VkCommandBuffer cmd() const { return cmd_; }
    VkImage image(GraphImage handle) const;
    VkImageView view(GraphImage handle) const;
    VkExtent2D extent(GraphImage handle) const;
    uint32_t textureIndex(GraphImage handle) const;
    VkBuffer buffer(GraphBuffer handle) const;

    const RenderGraph& graph_;
    const GraphPass& pass_;
    VkCommandBuffer cmd_ = VK_NULL_HANDLE;

   private:
    const GraphImageInfo& declared(GraphImage handle) const;
};
}  // namespace batap
