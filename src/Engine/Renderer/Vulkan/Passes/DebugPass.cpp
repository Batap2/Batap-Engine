#include "Renderer/Vulkan/Passes/DebugPass.h"

#include "Renderer/Vulkan/VulkanContext.h"
#include "Renderer/Vulkan/VulkanPipelines.h"
#include "Renderer/Vulkan/VulkanResources.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace batap
{
namespace
{
constexpr uint32_t kMaxDebugShapes = 65536;

void writeColor(float (&out)[4], const col3& c)
{
    out[0] = c.x();
    out[1] = c.y();
    out[2] = c.z();
    out[3] = 1.f;
}
}  // namespace

DebugPass::DebugPass(const PassSetup& setup) : setup_(setup)
{
    shapesBuffer_ = setup_.resources_.createPerFrameBuffer(
        sizeof(DebugShapeGPUData) * kMaxDebugShapes, "debugShapes");

    const std::vector<DebugVertexGPUData> verts = buildDebugWireframes(slices_);
    const uint64_t bytes = sizeof(DebugVertexGPUData) * verts.size();
    vertsBuffer_ = setup_.resources_.createStaticBuffer(bytes, "debugShapeVerts");
    std::memcpy(setup_.resources_.requestUpload(vertsBuffer_, bytes).data(), verts.data(), bytes);
}

DebugPass::~DebugPass()
{
    for (Layer& layer : layers_)
        vkDestroyPipeline(setup_.ctx_.device_, layer.pipeline_, nullptr);
    setup_.resources_.requestDestroy(vertsBuffer_);
    setup_.resources_.requestDestroy(shapesBuffer_);
}

void DebugPass::buildPipelines(const ShaderModules& modules)
{
    // No vertex input at all: geometry and instances are read from storage
    // buffers, indexed by SV_VertexID / SV_InstanceID. Depth is never written,
    // so wires cannot occlude the scene; the overlay layer skips the test.
    for (size_t i = 0; i < LayerCount; ++i)
    {
        if (layers_[i].pipeline_)
            vkDestroyPipeline(setup_.ctx_.device_, layers_[i].pipeline_, nullptr);

        const VkCompareOp compare = i == 0 ? VK_COMPARE_OP_LESS_OR_EQUAL : VK_COMPARE_OP_ALWAYS;
        layers_[i].pipeline_ = GraphicsPipelineBuilder()
                                   .shaders(modules[DebugVS], modules[DebugPS])
                                   .topology(VK_PRIMITIVE_TOPOLOGY_LINE_LIST)
                                   .colorFormat(setup_.colorFormat_)
                                   .depth(setup_.depthFormat_, false, compare)
                                   .build(setup_.ctx_.device_, setup_.layout_);
    }
}

void DebugPass::upload(const DebugDraw& depthTested, const DebugDraw& overlay)
{
    const std::array<const DebugDraw*, LayerCount> sources{&depthTested, &overlay};
    std::vector<DebugShapeGPUData> shapes;

    for (size_t i = 0; i < LayerCount; ++i)
    {
        Layer& layer = layers_[i];
        for (size_t kind = 0; kind < DebugDraw::ShapeCount; ++kind)
        {
            const auto& records = sources[i]->shapes(static_cast<DebugDraw::Shape>(kind));
            const size_t room = kMaxDebugShapes - shapes.size();

            layer.shapes_[kind].firstInstance_ = static_cast<uint32_t>(shapes.size());
            layer.shapes_[kind].instanceCount_ =
                static_cast<uint32_t>(std::min(records.size(), room));

            for (uint32_t r = 0; r < layer.shapes_[kind].instanceCount_; ++r)
            {
                DebugShapeGPUData& out = shapes.emplace_back();
                std::memcpy(out.world_, records[r].world_.data(), sizeof(out.world_));
                writeColor(out.color_, records[r].color_);
            }
        }
    }

    if (shapes.empty())
        return;

    const uint64_t bytes = sizeof(DebugShapeGPUData) * shapes.size();
    std::memcpy(setup_.resources_.requestUpload(shapesBuffer_, bytes).data(), shapes.data(), bytes);
}

void DebugPass::record(const PassContext& pass)
{
    DrawPush push = pass.push_;
    for (const Layer& layer : layers_)
    {
        bool bound = false;
        for (size_t kind = 0; kind < DebugDraw::ShapeCount; ++kind)
        {
            const Range& range = layer.shapes_[kind];
            if (range.instanceCount_ == 0)
                continue;

            if (!bound)
            {
                vkCmdBindPipeline(pass.cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, layer.pipeline_);
                bound = true;
            }
            push.instanceIndex_ = range.firstInstance_;
            pushDraw(pass, push);
            vkCmdDraw(pass.cmd_, slices_[kind].vertexCount_, range.instanceCount_,
                      slices_[kind].firstVertex_, 0);
        }
    }
}
}  // namespace batap
