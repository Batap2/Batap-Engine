#include "Renderer/Vulkan/RenderGraph.h"

#include "Renderer/Vulkan/VulkanFormats.h"
#include "Renderer/Vulkan/VulkanPipelines.h"
#include "Renderer/Vulkan/VulkanResources.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace batap
{
namespace
{
constexpr uint32_t NoBatch = UINT32_MAX;

bool canWrite(Usage usage)
{
    switch (usage)
    {
        case Usage::TransferDst:
        case Usage::ShaderWrite:
        case Usage::ColorAttachment:
        case Usage::DepthAttachment:
        case Usage::AccelerationStructureBuild:
            return true;
        case Usage::None:
        case Usage::TransferSrc:
        case Usage::ShaderRead:
        case Usage::Present:
        case Usage::AnyRead:
        case Usage::AccelerationStructureRead:
            return false;
    }
    return true;
}

bool hasLayout(Usage usage)
{
    return usage != Usage::AnyRead && usage != Usage::AccelerationStructureBuild &&
           usage != Usage::AccelerationStructureRead;
}

VkAttachmentLoadOp loadOp(Load load)
{
    switch (load)
    {
        case Load::Keep:
            return VK_ATTACHMENT_LOAD_OP_LOAD;
        case Load::Clear:
            return VK_ATTACHMENT_LOAD_OP_CLEAR;
        case Load::DontCare:
            return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    }
    return VK_ATTACHMENT_LOAD_OP_LOAD;
}

std::logic_error passError(const std::string& pass, const std::string& what)
{
    return std::logic_error("render graph, pass '" + pass + "': " + what);
}

bool contains(const std::vector<Usage>& usages, Usage usage)
{
    return std::find(usages.begin(), usages.end(), usage) != usages.end();
}

struct Tracking
{
    bool hasContent_ = false;
    uint32_t writerBatch_ = NoBatch;
    uint32_t seenBy_ = 0;
    std::vector<uint32_t> readerBatches_;
    uint32_t lastBatch_ = NoBatch;
};

struct ImageState
{
    Usage usage_ = Usage::None;
    bool written_ = false;
    Tracking tracking_;
};

struct BufferState
{
    Usage write_ = Usage::None;
    std::vector<Usage> visible_;
    std::vector<Usage> reads_;
    Tracking tracking_;
};

bool syncQueues(std::vector<GraphBatch>& batches, Tracking& tracking, bool writes,
                uint32_t batchIndex, const std::string& pass, const std::string& resource)
{
    const uint32_t queue = batches[batchIndex].queue_;
    const uint32_t queueBit = 1u << queue;
    bool waited = false;
    const auto waitOn = [&](uint32_t producer)
    {
        if (producer == NoBatch || batches[producer].queue_ == queue)
            return;
        batches[producer].signals_ = true;
        std::vector<uint32_t>& waits = batches[batchIndex].waits_;
        if (std::find(waits.begin(), waits.end(), producer) == waits.end())
            waits.push_back(producer);
        waited = true;
    };

    if ((tracking.seenBy_ & queueBit) == 0)
    {
        if (tracking.writerBatch_ == NoBatch && tracking.hasContent_)
            throw passError(pass, "'" + resource +
                                      "' comes from outside the graph and is first used on "
                                      "another queue than the graphics one");
        waitOn(tracking.writerBatch_);
    }
    if (writes)
        for (const uint32_t reader : tracking.readerBatches_)
            waitOn(reader);

    if (writes)
    {
        tracking.hasContent_ = true;
        tracking.writerBatch_ = batchIndex;
        tracking.seenBy_ = queueBit;
        tracking.readerBatches_.clear();
    }
    else
    {
        tracking.seenBy_ |= queueBit;
        tracking.readerBatches_.push_back(batchIndex);
    }
    tracking.lastBatch_ = batchIndex;
    return waited;
}
}  // namespace

GraphPass& GraphPass::read(GraphImage handle, Usage usage)
{
    accesses_.push_back({handle.index_, true, usage, true, false, false});
    return *this;
}

GraphPass& GraphPass::write(GraphImage handle, Usage usage)
{
    accesses_.push_back({handle.index_, true, usage, false, true, false});
    return *this;
}

GraphPass& GraphPass::read(GraphBuffer handle, Usage usage)
{
    accesses_.push_back({handle.index_, false, usage, true, false, false});
    return *this;
}

GraphPass& GraphPass::write(GraphBuffer handle, Usage usage)
{
    accesses_.push_back({handle.index_, false, usage, false, true, false});
    return *this;
}

GraphPass& GraphPass::color(GraphImage handle, Load load, VkClearColorValue clear)
{
    GraphAttachment& attachment = colors_.emplace_back();
    attachment.image_ = handle;
    attachment.load_ = load;
    attachment.clear_.color = clear;
    accesses_.push_back({handle.index_, true, Usage::ColorAttachment, load == Load::Keep, true,
                         load != Load::Keep});
    return *this;
}

GraphPass& GraphPass::depth(GraphImage handle, Load load, float clear)
{
    GraphAttachment& attachment = depth_.emplace();
    attachment.image_ = handle;
    attachment.load_ = load;
    attachment.clear_.depthStencil = {clear, 0};
    accesses_.push_back({handle.index_, true, Usage::DepthAttachment, load == Load::Keep, true,
                         load != Load::Keep});
    return *this;
}

GraphPass& GraphPass::execute(std::function<void(GraphContext&)> fn)
{
    execute_ = std::move(fn);
    return *this;
}

RenderGraph::RenderGraph(std::array<uint32_t, GraphQueueCount> queues) : queues_(queues) {}

GraphImage RenderGraph::importImage(std::string name, const GraphImageInfo& info, Usage in,
                                    Usage out)
{
    images_.push_back({std::move(name), info, in, out});
    compiled_ = false;
    return GraphImage{static_cast<uint32_t>(images_.size() - 1)};
}

GraphImage RenderGraph::importImage(std::string name, ResourceManager& resources,
                                    GPUResourceHandle image, Usage in, Usage out)
{
    GraphImageInfo info;
    info.image_ = resources.imageFor(image);
    info.view_ = resources.viewFor(image);
    info.extent_ = resources.extentFor(image);
    info.aspect_ = isDepthFormat(resources.formatFor(image)) ? VK_IMAGE_ASPECT_DEPTH_BIT
                                                             : VK_IMAGE_ASPECT_COLOR_BIT;
    info.textureIndex_ = resources.textureIndex(image);
    return importImage(std::move(name), info, in, out);
}

GraphImage RenderGraph::importImage(std::string name, const GraphImageInfo& info, Usage out)
{
    const auto kept = keptUsages_.find(info.image_);
    const GraphImage handle = importImage(
        std::move(name), info, kept == keptUsages_.end() ? Usage::None : kept->second, out);
    images_.back().kept_ = true;
    return handle;
}

void RenderGraph::forget(VkImage image)
{
    keptUsages_.erase(image);
}

GraphBuffer RenderGraph::importBuffer(std::string name, VkBuffer buffer, Usage in, Usage out)
{
    buffers_.push_back({std::move(name), buffer, in, out});
    compiled_ = false;
    return GraphBuffer{static_cast<uint32_t>(buffers_.size() - 1)};
}

GraphBuffer RenderGraph::importBuffer(std::string name, ResourceManager& resources,
                                      GPUResourceHandle buffer, Usage in, Usage out)
{
    return importBuffer(std::move(name), resources.bufferFor(buffer), in, out);
}

GraphPass& RenderGraph::addPass(std::string name, GraphQueue queue)
{
    GraphPass& pass = passes_.emplace_back();
    pass.name_ = std::move(name);
    pass.queue_ = queue;
    compiled_ = false;
    return pass;
}

void RenderGraph::reset()
{
    images_.clear();
    buffers_.clear();
    passes_.clear();
    batches_.clear();
    compiled_ = false;
}

uint32_t RenderGraph::textureIndex(GraphImage handle) const
{
    return images_.at(handle.index_).info_.textureIndex_;
}

const std::string& RenderGraph::resourceName(const GraphAccess& access) const
{
    return access.image_ ? images_[access.resource_].name_ : buffers_[access.resource_].name_;
}

void RenderGraph::compile()
{
    batches_.clear();
    for (uint32_t p = 0; p < passes_.size(); ++p)
    {
        GraphPass& pass = passes_[p];
        const uint32_t queue = queues_[static_cast<size_t>(pass.queue_)];
        if (batches_.empty() || batches_.back().queue_ != queue)
            batches_.push_back({queue, p, 0, {}, false, {}});
        ++batches_.back().passCount_;
        pass.batch_ = static_cast<uint32_t>(batches_.size() - 1);
        pass.barriers_.clear();
    }

    const uint32_t graphicsBit = 1u << queues_[static_cast<size_t>(GraphQueue::Graphics)];
    std::vector<ImageState> imageStates(images_.size());
    for (size_t i = 0; i < images_.size(); ++i)
    {
        imageStates[i].usage_ = images_[i].in_;
        imageStates[i].written_ = canWrite(images_[i].in_);
        imageStates[i].tracking_.hasContent_ = images_[i].in_ != Usage::None;
        imageStates[i].tracking_.seenBy_ = graphicsBit;
    }
    std::vector<BufferState> bufferStates(buffers_.size());
    for (size_t i = 0; i < buffers_.size(); ++i)
    {
        const Usage in = buffers_[i].in_;
        if (canWrite(in))
            bufferStates[i].write_ = in;
        else if (in != Usage::None)
            bufferStates[i].reads_.push_back(in);
        bufferStates[i].tracking_.hasContent_ = in != Usage::None;
        bufferStates[i].tracking_.seenBy_ = graphicsBit;
    }

    std::vector<uint32_t> lastImagePass(images_.size(), NoBatch);
    for (uint32_t p = 0; p < passes_.size(); ++p)
    {
        const GraphPass& pass = passes_[p];
        for (size_t i = 0; i < pass.accesses_.size(); ++i)
        {
            const GraphAccess& access = pass.accesses_[i];
            if (access.resource_ >= (access.image_ ? images_.size() : buffers_.size()))
                throw passError(pass.name_, "uses a resource this graph did not import");
            for (size_t j = i + 1; j < pass.accesses_.size(); ++j)
                if (pass.accesses_[j].image_ == access.image_ &&
                    pass.accesses_[j].resource_ == access.resource_)
                    throw passError(pass.name_, "uses '" + resourceName(access) + "' twice");
            if (access.image_ && !hasLayout(access.usage_))
                throw passError(pass.name_, "'" + resourceName(access) +
                                                "' is an image: its usage needs a layout");
            if (access.image_)
                lastImagePass[access.resource_] = p;
        }
    }

    for (uint32_t p = 0; p < passes_.size(); ++p)
    {
        GraphPass& pass = passes_[p];

        VkExtent2D extent{};
        const auto checkAttachment = [&](const GraphAttachment& attachment, bool isDepth)
        {
            const ImageResource& image = images_[attachment.image_.index_];
            const bool depthAspect = (image.info_.aspect_ & VK_IMAGE_ASPECT_DEPTH_BIT) != 0;
            if (depthAspect != isDepth)
                throw passError(pass.name_, "'" + image.name_ +
                                                (isDepth ? "' is a colour image used as the "
                                                           "depth attachment"
                                                         : "' is a depth image used as a "
                                                           "colour attachment"));
            const VkExtent2D size = image.info_.extent_;
            if (size.width == 0 || size.height == 0)
                return;
            if (extent.width != 0 && (extent.width != size.width || extent.height != size.height))
                throw passError(pass.name_, "its attachments differ in size");
            extent = size;
        };
        for (const GraphAttachment& attachment : pass.colors_)
            checkAttachment(attachment, false);
        if (pass.depth_)
            checkAttachment(*pass.depth_, true);

        for (const GraphAccess& access : pass.accesses_)
        {
            const std::string& name = resourceName(access);
            if (access.image_)
            {
                ImageState& state = imageStates[access.resource_];
                if (access.reads_ && !state.tracking_.hasContent_)
                    throw passError(pass.name_, "reads '" + name + "' before anything writes it");
                const bool layoutChange = usageInfo(state.usage_).layout !=
                                          usageInfo(access.usage_).layout;
                const bool needed = layoutChange || state.written_ || access.writes_;
                const GraphBarrier barrier{access.resource_, true, state.usage_, access.usage_,
                                           access.discards_, false};
                const bool waited = syncQueues(batches_, state.tracking_, access.writes_,
                                               pass.batch_, pass.name_, name);
                if (needed)
                {
                    pass.barriers_.push_back(barrier);
                    pass.barriers_.back().afterWait_ = waited;
                }
                state.usage_ = access.usage_;
                state.written_ = access.writes_;
            }
            else
            {
                BufferState& state = bufferStates[access.resource_];
                if (access.reads_ && !state.tracking_.hasContent_)
                    throw passError(pass.name_, "reads '" + name + "' before anything writes it");
                std::vector<Usage> from;
                if (access.writes_)
                {
                    if (state.write_ != Usage::None)
                        from.push_back(state.write_);
                    for (const Usage read : state.reads_)
                        if (!contains(from, read))
                            from.push_back(read);
                    state.write_ = access.usage_;
                    state.visible_.clear();
                    state.reads_.clear();
                }
                else
                {
                    if (state.write_ != Usage::None && !contains(state.visible_, access.usage_))
                    {
                        from.push_back(state.write_);
                        state.visible_.push_back(access.usage_);
                    }
                    if (!contains(state.reads_, access.usage_))
                        state.reads_.push_back(access.usage_);
                }
                const bool waited = syncQueues(batches_, state.tracking_, access.writes_,
                                               pass.batch_, pass.name_, name);
                for (const Usage source : from)
                    pass.barriers_.push_back(
                        {access.resource_, false, source, access.usage_, false, waited});
            }
        }

        const auto storeOp = [&](GraphAttachment& attachment)
        {
            const uint32_t image = attachment.image_.index_;
            attachment.store_ = lastImagePass[image] > p || images_[image].out_ != Usage::None
                                    ? VK_ATTACHMENT_STORE_OP_STORE
                                    : VK_ATTACHMENT_STORE_OP_DONT_CARE;
        };
        for (GraphAttachment& attachment : pass.colors_)
            storeOp(attachment);
        if (pass.depth_)
            storeOp(*pass.depth_);
    }

    if (!batches_.empty())
    {
        const auto exitBatch = [&](const Tracking& tracking) -> GraphBatch&
        {
            return batches_[tracking.lastBatch_ == NoBatch ? batches_.size() - 1
                                                           : tracking.lastBatch_];
        };
        for (uint32_t i = 0; i < images_.size(); ++i)
        {
            const ImageState& state = imageStates[i];
            const Usage out = images_[i].out_;
            if (out == Usage::None ||
                (state.tracking_.lastBatch_ == NoBatch && out == images_[i].in_))
                continue;
            const bool layoutChange = usageInfo(state.usage_).layout != usageInfo(out).layout;
            if (layoutChange || state.written_ || canWrite(out))
                exitBatch(state.tracking_)
                    .exits_.push_back({i, true, state.usage_, out, false, false});
        }
        for (uint32_t i = 0; i < buffers_.size(); ++i)
        {
            const BufferState& state = bufferStates[i];
            const Usage out = buffers_[i].out_;
            if (out == Usage::None || state.tracking_.lastBatch_ == NoBatch)
                continue;
            std::vector<Usage> from;
            if (canWrite(out))
            {
                if (state.write_ != Usage::None)
                    from.push_back(state.write_);
                for (const Usage read : state.reads_)
                    if (!contains(from, read))
                        from.push_back(read);
            }
            else if (state.write_ != Usage::None && !contains(state.visible_, out))
                from.push_back(state.write_);
            for (const Usage source : from)
                exitBatch(state.tracking_).exits_.push_back({i, false, source, out, false, false});
        }
    }

    for (size_t i = 0; i < images_.size(); ++i)
        if (images_[i].kept_)
            keptUsages_[images_[i].info_.image_] =
                images_[i].out_ != Usage::None ? images_[i].out_ : imageStates[i].usage_;

    compiled_ = true;
}

void RenderGraph::recordBarriers(VkCommandBuffer cmd,
                                 const std::vector<GraphBarrier>& barriers) const
{
    std::vector<VkImageMemoryBarrier2> imageBarriers;
    VkMemoryBarrier2 memory{};
    memory.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    bool hasMemory = false;

    for (const GraphBarrier& barrier : barriers)
    {
        if (barrier.afterWait_)
            throw std::logic_error(
                "render graph: a barrier after a wait on another queue needs "
                "the batches submitted apart");
        const UsageInfo src = usageInfo(barrier.from_);
        const UsageInfo dst = usageInfo(barrier.to_);
        if (barrier.image_)
        {
            const GraphImageInfo& info = images_[barrier.resource_].info_;
            VkImageMemoryBarrier2& b = imageBarriers.emplace_back();
            b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            b.srcStageMask = src.stage;
            b.srcAccessMask = src.access;
            b.dstStageMask = dst.stage;
            b.dstAccessMask = dst.access;
            b.oldLayout = barrier.discard_ ? VK_IMAGE_LAYOUT_UNDEFINED : src.layout;
            b.newLayout = dst.layout;
            b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.image = info.image_;
            b.subresourceRange = {info.aspect_, 0, VK_REMAINING_MIP_LEVELS, 0,
                                  VK_REMAINING_ARRAY_LAYERS};
        }
        else
        {
            memory.srcStageMask |= src.stage;
            memory.srcAccessMask |= src.access;
            memory.dstStageMask |= dst.stage;
            memory.dstAccessMask |= dst.access;
            hasMemory = true;
        }
    }
    if (imageBarriers.empty() && !hasMemory)
        return;

    VkDependencyInfo dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.memoryBarrierCount = hasMemory ? 1u : 0u;
    dependency.pMemoryBarriers = hasMemory ? &memory : nullptr;
    dependency.imageMemoryBarrierCount = static_cast<uint32_t>(imageBarriers.size());
    dependency.pImageMemoryBarriers = imageBarriers.empty() ? nullptr : imageBarriers.data();
    vkCmdPipelineBarrier2(cmd, &dependency);
}

void RenderGraph::recordPass(GraphPass& pass, VkCommandBuffer cmd)
{
    recordBarriers(cmd, pass.barriers_);

    const bool rendering = !pass.colors_.empty() || pass.depth_.has_value();
    if (rendering)
    {
        VkExtent2D extent{};
        const auto attachmentInfo = [&](const GraphAttachment& attachment, Usage usage)
        {
            const GraphImageInfo& image = images_[attachment.image_.index_].info_;
            extent = image.extent_;
            VkRenderingAttachmentInfo info{};
            info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            info.imageView = image.view_;
            info.imageLayout = usageInfo(usage).layout;
            info.loadOp = loadOp(attachment.load_);
            info.storeOp = attachment.store_;
            info.clearValue = attachment.clear_;
            return info;
        };

        std::vector<VkRenderingAttachmentInfo> colors;
        colors.reserve(pass.colors_.size());
        for (const GraphAttachment& attachment : pass.colors_)
            colors.push_back(attachmentInfo(attachment, Usage::ColorAttachment));
        VkRenderingAttachmentInfo depthInfo{};
        if (pass.depth_)
            depthInfo = attachmentInfo(*pass.depth_, Usage::DepthAttachment);

        VkRenderingInfo info{};
        info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        info.renderArea = {{0, 0}, extent};
        info.layerCount = 1;
        info.colorAttachmentCount = static_cast<uint32_t>(colors.size());
        info.pColorAttachments = colors.empty() ? nullptr : colors.data();
        info.pDepthAttachment = pass.depth_ ? &depthInfo : nullptr;
        vkCmdBeginRendering(cmd, &info);
        setViewportYUp(cmd, extent.width, extent.height);
    }

    if (pass.execute_)
    {
        GraphContext context{*this, pass, cmd};
        pass.execute_(context);
    }

    if (rendering)
        vkCmdEndRendering(cmd);
}

void RenderGraph::record(uint32_t batchIndex, VkCommandBuffer cmd)
{
    if (!compiled_)
        compile();
    const GraphBatch& batch = batches_.at(batchIndex);
    if (!batch.waits_.empty())
        throw std::logic_error(
            "render graph: a batch that waits on another queue needs the "
            "batches submitted apart");
    for (uint32_t p = batch.firstPass_; p < batch.firstPass_ + batch.passCount_; ++p)
        recordPass(passes_[p], cmd);
    recordBarriers(cmd, batch.exits_);
}

void RenderGraph::record(VkCommandBuffer cmd)
{
    if (!compiled_)
        compile();
    if (batches_.size() > 1)
        throw std::logic_error(
            "render graph: its passes run on several queues, each batch "
            "has to be recorded and submitted on its own");
    if (!batches_.empty())
        record(0, cmd);
}

const GraphImageInfo& GraphContext::declared(GraphImage handle) const
{
    const bool found = std::any_of(pass_.accesses_.begin(), pass_.accesses_.end(),
                                   [&](const GraphAccess& access)
                                   { return access.image_ && access.resource_ == handle.index_; });
    if (!found || handle.index_ >= graph_.images_.size())
        throw passError(pass_.name_, "uses the image '" +
                                         (handle.index_ < graph_.images_.size()
                                              ? graph_.images_[handle.index_].name_
                                              : std::string("?")) +
                                         "' without declaring it");
    return graph_.images_[handle.index_].info_;
}

VkImage GraphContext::image(GraphImage handle) const
{
    return declared(handle).image_;
}

VkImageView GraphContext::view(GraphImage handle) const
{
    return declared(handle).view_;
}

VkExtent2D GraphContext::extent(GraphImage handle) const
{
    return declared(handle).extent_;
}

uint32_t GraphContext::textureIndex(GraphImage handle) const
{
    return declared(handle).textureIndex_;
}

VkBuffer GraphContext::buffer(GraphBuffer handle) const
{
    const bool found = std::any_of(pass_.accesses_.begin(), pass_.accesses_.end(),
                                   [&](const GraphAccess& access)
                                   { return !access.image_ && access.resource_ == handle.index_; });
    if (!found || handle.index_ >= graph_.buffers_.size())
        throw passError(pass_.name_, "uses the buffer '" +
                                         (handle.index_ < graph_.buffers_.size()
                                              ? graph_.buffers_[handle.index_].name_
                                              : std::string("?")) +
                                         "' without declaring it");
    return graph_.buffers_[handle.index_].buffer_;
}
}  // namespace batap
