#include "VulkanScenePasses.h"

#include "VulkanContext.h"
#include "VulkanFormats.h"
#include "VulkanPipelines.h"
#include "VulkanResources.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <span>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include "Assets/AssetManager.h"
#include "Assets/Mesh.h"
#include "Components/Camera_C.h"
#include "Components/Mesh_C.h"
#include "Components/Skybox_C.h"
#include "Components/Transform_C.h"
#include "DebugUtils.h"
#include "Instance/InstanceManager.h"
#include "Paths.h"
#include "Renderer/Billboards.h"
#include "Renderer/DebugDraw.h"
#include "Shaders/ShaderInterop.h"

namespace batap
{
namespace
{

constexpr uint32_t kMaxDebugShapes = 65536;
constexpr uint32_t kMaxBillboards = 65536;

// Unit wireframes, built once. A box is the [-1, 1] cube, so its matrix
// carries the half extents.
constexpr std::array<std::array<float, 3>, 8> kBoxCorners = {{{-1, -1, -1},
                                                             {1, -1, -1},
                                                             {1, 1, -1},
                                                             {-1, 1, -1},
                                                             {-1, -1, 1},
                                                             {1, -1, 1},
                                                             {1, 1, 1},
                                                             {-1, 1, 1}}};
constexpr std::array<uint32_t, 24> kBoxEdges = {0, 1, 1, 2, 2, 3, 3, 0, 4, 5, 5, 6,
                                                6, 7, 7, 4, 0, 4, 1, 5, 2, 6, 3, 7};

constexpr uint32_t kRingSegments = 32;

void writeVertex(DebugVertexGPUData& out, float x, float y, float z)
{
    out.pos_[0] = x;
    out.pos_[1] = y;
    out.pos_[2] = z;
    out.pad_ = 0.f;
}

void writeVertex(DebugVertexGPUData& out, const std::array<float, 3>& p)
{
    writeVertex(out, p[0], p[1], p[2]);
}

// axis 0/1/2 says which coordinate stays at zero, so the three calls give the
// YZ, XZ and XY great circles.
void appendArc(std::vector<DebugVertexGPUData>& verts, uint32_t axis, float fromRadians,
               float toRadians, uint32_t segments)
{
    auto at = [&](float angle)
    {
        const float c = std::cos(angle);
        const float sn = std::sin(angle);
        switch (axis)
        {
            case 0:
                return std::array<float, 3>{0.f, c, sn};
            case 1:
                return std::array<float, 3>{c, 0.f, sn};
            default:
                return std::array<float, 3>{c, sn, 0.f};
        }
    };

    const float step = (toRadians - fromRadians) / static_cast<float>(segments);
    for (uint32_t i = 0; i < segments; ++i)
    {
        writeVertex(verts.emplace_back(), at(fromRadians + step * static_cast<float>(i)));
        writeVertex(verts.emplace_back(), at(fromRadians + step * static_cast<float>(i + 1)));
    }
}

void writeColor(float (&out)[4], const col3& c)
{
    out[0] = c.x();
    out[1] = c.y();
    out[2] = c.z();
    out[3] = 1.f;
}

}  // namespace

ScenePasses::ScenePasses(VulkanContext& ctx, ResourceManager& resources, VkFormat colorFormat,
                         VkFormat depthFormat)
    : ctx_(ctx), resources_(resources), colorFormat_(colorFormat), depthFormat_(depthFormat)
{
    // ---- Set 1 : 5 storage buffers, visibles VS+PS ----
    std::array<VkDescriptorSetLayoutBinding, FrameSetBindingCount> bindings{};
    for (uint32_t i = 0; i < FrameSetBindingCount; ++i)
    {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = FrameSetBindingCount;
    layoutInfo.pBindings = bindings.data();
    if (vkCreateDescriptorSetLayout(ctx_.device_, &layoutInfo, nullptr, &frameSetLayout_) !=
        VK_SUCCESS)
        throw std::runtime_error("ScenePasses : frame set layout");

    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                  FrameSetBindingCount * FramesInFlight};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = FramesInFlight;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    if (vkCreateDescriptorPool(ctx_.device_, &poolInfo, nullptr, &framePool_) != VK_SUCCESS)
        throw std::runtime_error("ScenePasses : frame pool");

    frameSets_.resize(FramesInFlight);
    std::vector<VkDescriptorSetLayout> layouts(FramesInFlight, frameSetLayout_);
    VkDescriptorSetAllocateInfo setInfo{};
    setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    setInfo.descriptorPool = framePool_;
    setInfo.descriptorSetCount = FramesInFlight;
    setInfo.pSetLayouts = layouts.data();
    if (vkAllocateDescriptorSets(ctx_.device_, &setInfo, frameSets_.data()) != VK_SUCCESS)
        throw std::runtime_error("ScenePasses : frame sets");

    // ---- Pipeline layout partagé (set 0 bindless + set 1 + push) ----
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.size = sizeof(DrawPush);

    const VkDescriptorSetLayout setLayouts[2] = {resources_.textureSetLayout(), frameSetLayout_};
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 2;
    pipelineLayoutInfo.pSetLayouts = setLayouts;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushRange;
    if (vkCreatePipelineLayout(ctx_.device_, &pipelineLayoutInfo, nullptr, &pipelineLayout_) !=
        VK_SUCCESS)
        throw std::runtime_error("ScenePasses : pipeline layout");

    // ---- Pipelines (SPIR-V compilé au build, à côté de l'exe) ----
    const std::string shaderDir = resolveEngineFile("shaders", "shaders");
    const ShaderModule vs{ctx_.device_, shaderDir + "/VertexShader.spv"};
    const ShaderModule ps{ctx_.device_, shaderDir + "/PixelShader.spv"};
    const ShaderModule skyVS{ctx_.device_, shaderDir + "/SkyVS.spv"};
    const ShaderModule skyPS{ctx_.device_, shaderDir + "/SkyPS.spv"};
    const ShaderModule debugVS{ctx_.device_, shaderDir + "/DebugShapeVS.spv"};
    const ShaderModule debugPS{ctx_.device_, shaderDir + "/DebugPS.spv"};
    const ShaderModule billboardVS{ctx_.device_, shaderDir + "/BillboardVS.spv"};
    const ShaderModule billboardPS{ctx_.device_, shaderDir + "/BillboardPS.spv"};
    buildPipelines(vs, ps, skyVS, skyPS, debugVS, debugPS, billboardVS, billboardPS);

    debugShapesBuffer_ = resources_.createPerFrameBuffer(
        sizeof(DebugShapeGPUData) * kMaxDebugShapes, "debugShapes");
    billboardBuffer_ =
        resources_.createPerFrameBuffer(sizeof(BillboardGPUData) * kMaxBillboards, "billboards");
    buildDebugGeometry();
}

void ScenePasses::buildPipelines(VkShaderModule vs, VkShaderModule ps, VkShaderModule skyVS,
                                 VkShaderModule skyPS, VkShaderModule debugVS,
                                 VkShaderModule debugPS, VkShaderModule billboardVS,
                                 VkShaderModule billboardPS)
{
    if (geometryPipeline_)
        vkDestroyPipeline(ctx_.device_, geometryPipeline_, nullptr);
    if (skyPipeline_)
        vkDestroyPipeline(ctx_.device_, skyPipeline_, nullptr);
    if (billboardPipeline_)
        vkDestroyPipeline(ctx_.device_, billboardPipeline_, nullptr);
    for (DebugLayer& layer : debugLayers_)
        if (layer.pipeline_)
            vkDestroyPipeline(ctx_.device_, layer.pipeline_, nullptr);

    geometryPipeline_ = GraphicsPipelineBuilder()
                            .shaders(vs, ps)
                            // Locations follow Mesh::Stream — changing one means
                            // changing the other.
                            .vertexAttribute(Mesh::Position, VK_FORMAT_R32G32B32_SFLOAT, 12)
                            .vertexAttribute(Mesh::Normal, VK_FORMAT_R32G32B32_SFLOAT, 12)
                            .vertexAttribute(Mesh::UV0, VK_FORMAT_R32G32_SFLOAT, 8)
                            .vertexAttribute(Mesh::Tangent, VK_FORMAT_R32G32B32A32_SFLOAT, 16)
                            .colorFormat(colorFormat_)
                            .depth(depthFormat_, true, VK_COMPARE_OP_LESS)
                            .cullBack()
                            .build(ctx_.device_, pipelineLayout_);

    skyPipeline_ = GraphicsPipelineBuilder()
                       .shaders(skyVS, skyPS)
                       .colorFormat(colorFormat_)
                       .depth(depthFormat_, false, VK_COMPARE_OP_LESS_OR_EQUAL)
                       .build(ctx_.device_, pipelineLayout_);

    // Not culled: a Y-locked quad is seen from behind as soon as the camera
    // passes it.
    billboardPipeline_ = GraphicsPipelineBuilder()
                             .shaders(billboardVS, billboardPS)
                             .colorFormat(colorFormat_)
                             .depth(depthFormat_, true, VK_COMPARE_OP_LESS)
                             .build(ctx_.device_, pipelineLayout_);

    // No vertex input at all: geometry and instances are read from storage
    // buffers, indexed by SV_VertexID / SV_InstanceID. Depth is never written,
    // so wires cannot occlude the scene; the overlay layer skips the test.
    for (size_t i = 0; i < DebugLayerCount; ++i)
    {
        const VkCompareOp compare = i == 0 ? VK_COMPARE_OP_LESS_OR_EQUAL : VK_COMPARE_OP_ALWAYS;
        debugLayers_[i].pipeline_ = GraphicsPipelineBuilder()
                                        .shaders(debugVS, debugPS)
                                        .topology(VK_PRIMITIVE_TOPOLOGY_LINE_LIST)
                                        .colorFormat(colorFormat_)
                                        .depth(depthFormat_, false, compare)
                                        .build(ctx_.device_, pipelineLayout_);
    }
}

void ScenePasses::checkHotReload()
{
    namespace fs = std::filesystem;

    // Les sources HLSL de l'arbre (pas les .spv du build) : le hot reload est
    // un outil de dev, il vit là où on édite.
    const fs::path sourceDir = fs::path(BATAP_ROOT_DIR) / "src/Engine/Shaders";
    struct Stage
    {
        const char* file;
        const char* target;
    };
    static constexpr std::array<Stage, 8> stages = {{
        {"VertexShader.hlsl", "vs_6_6"},
        {"PixelShader.hlsl", "ps_6_6"},
        {"SkyVS.hlsl", "vs_6_6"},
        {"SkyPS.hlsl", "ps_6_6"},
        {"DebugShapeVS.hlsl", "vs_6_6"},
        {"DebugPS.hlsl", "ps_6_6"},
        {"BillboardVS.hlsl", "vs_6_6"},
        {"BillboardPS.hlsl", "ps_6_6"},
    }};

    // Tout le dossier : un header partagé déclenche le reload comme une source.
    fs::file_time_type latest{};
    std::error_code ec;
    for (const fs::directory_entry& entry : fs::directory_iterator(sourceDir, ec))
        if (entry.is_regular_file(ec))
            latest = std::max(latest, entry.last_write_time(ec));

    if (shadersMtime_ == fs::file_time_type{})
    {
        shadersMtime_ = latest;  // baseline au premier appel, pas de rebuild
        return;
    }
    if (latest <= shadersMtime_)
        return;
    shadersMtime_ = latest;  // même en cas d'échec : on retentera à la
                             // prochaine sauvegarde, pas à chaque check

    std::array<std::vector<uint8_t>, stages.size()> spirv;
    for (size_t i = 0; i < stages.size(); ++i)
    {
        spirv[i] =
            ctx_.shaderCompiler_.compile((sourceDir / stages[i].file).string(), stages[i].target);
        if (spirv[i].empty())
        {
            std::cerr << "[ShaderCompiler] " << stages[i].file
                      << " : compilation échouée — pipelines conservées\n";
            return;
        }
    }

    vkDeviceWaitIdle(ctx_.device_);
    const ShaderModule vs{ctx_.device_, spirv[0].data(), spirv[0].size()};
    const ShaderModule ps{ctx_.device_, spirv[1].data(), spirv[1].size()};
    const ShaderModule skyVS{ctx_.device_, spirv[2].data(), spirv[2].size()};
    const ShaderModule skyPS{ctx_.device_, spirv[3].data(), spirv[3].size()};
    const ShaderModule debugVS{ctx_.device_, spirv[4].data(), spirv[4].size()};
    const ShaderModule debugPS{ctx_.device_, spirv[5].data(), spirv[5].size()};
    const ShaderModule billboardVS{ctx_.device_, spirv[6].data(), spirv[6].size()};
    const ShaderModule billboardPS{ctx_.device_, spirv[7].data(), spirv[7].size()};
    buildPipelines(vs, ps, skyVS, skyPS, debugVS, debugPS, billboardVS, billboardPS);
    std::cout << "[ShaderCompiler] shaders reloaded" << std::endl;
}

ScenePasses::~ScenePasses()
{
    vkDestroyPipeline(ctx_.device_, geometryPipeline_, nullptr);
    vkDestroyPipeline(ctx_.device_, skyPipeline_, nullptr);
    vkDestroyPipeline(ctx_.device_, billboardPipeline_, nullptr);
    for (DebugLayer& layer : debugLayers_)
        vkDestroyPipeline(ctx_.device_, layer.pipeline_, nullptr);
    resources_.requestDestroy(billboardBuffer_);
    resources_.requestDestroy(debugVertsBuffer_);
    resources_.requestDestroy(debugShapesBuffer_);
    vkDestroyPipelineLayout(ctx_.device_, pipelineLayout_, nullptr);
    vkDestroyDescriptorPool(ctx_.device_, framePool_, nullptr);
    vkDestroyDescriptorSetLayout(ctx_.device_, frameSetLayout_, nullptr);
}

void ScenePasses::writeFrameSet(uint32_t frame, const SceneRenderArgs& args, Engine& ctx)
{
    auto* instanceM = args.instanceManager_;

    // Every binding must be claimed exactly once: written twice one of the two
    // buffers is silently lost, never written the shader reads a null buffer.
    // No driver reports either, so this is the only place it can be caught.
    std::array<VkBuffer, FrameSetBindingCount> buffers{};
    auto claim = [&](uint32_t binding, VkBuffer buffer)
    {
        if (binding >= FrameSetBindingCount || buffers[binding] != VK_NULL_HANDLE)
            ThrowRuntime("frame set: two writers claim the same binding");
        buffers[binding] = buffer;
    };

    instanceM->forEachPool(
        [&](auto& pool)
        {
            using InstanceT = typename std::remove_reference_t<decltype(pool)>::InstanceType;
            claim(InstanceT::Binding, resources_.bufferFor(pool.instancePoolHandle_));
        });
    claim(MaterialsBinding,
          resources_.bufferFor(ctx.assetManager_->getGPUArena<Material>()->bufferHandle()));
    claim(DebugShapeVertsBinding, resources_.bufferFor(debugVertsBuffer_));
    claim(DebugShapesBinding, resources_.bufferFor(debugShapesBuffer_));
    claim(BillboardsBinding, resources_.bufferFor(billboardBuffer_));

    std::array<VkDescriptorBufferInfo, FrameSetBindingCount> bufferInfos{};
    std::array<VkWriteDescriptorSet, FrameSetBindingCount> writes{};
    for (uint32_t i = 0; i < FrameSetBindingCount; ++i)
    {
        if (buffers[i] == VK_NULL_HANDLE)
            ThrowRuntime("frame set: a binding has no buffer behind it");

        bufferInfos[i].buffer = buffers[i];
        bufferInfos[i].range = VK_WHOLE_SIZE;

        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = frameSets_[frame];
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[i].pBufferInfo = &bufferInfos[i];
    }
    vkUpdateDescriptorSets(ctx_.device_, FrameSetBindingCount, writes.data(), 0, nullptr);
}

void ScenePasses::buildDebugGeometry()
{
    constexpr float kPi = 3.14159265358979f;
    std::vector<DebugVertexGPUData> verts;

    auto begin = [&](DebugDraw::Shape shape) -> DebugShapeGeometry&
    {
        DebugShapeGeometry& geom = debugGeometry_[static_cast<size_t>(shape)];
        geom.firstVertex_ = static_cast<uint32_t>(verts.size());
        return geom;
    };
    auto end = [&](DebugShapeGeometry& geom)
    { geom.vertexCount_ = static_cast<uint32_t>(verts.size()) - geom.firstVertex_; };

    DebugShapeGeometry& line = begin(DebugDraw::Shape::Line);
    writeVertex(verts.emplace_back(), 0.f, 0.f, 0.f);
    writeVertex(verts.emplace_back(), 1.f, 0.f, 0.f);
    end(line);

    DebugShapeGeometry& box = begin(DebugDraw::Shape::Box);
    for (uint32_t index : kBoxEdges)
        writeVertex(verts.emplace_back(), kBoxCorners[index]);
    end(box);

    DebugShapeGeometry& sphere = begin(DebugDraw::Shape::Sphere);
    for (uint32_t axis = 0; axis < 3; ++axis)
        appendArc(verts, axis, 0.f, 2.f * kPi, kRingSegments);
    end(sphere);

    // Dome pointing +Y: the equator, then two meridians.
    DebugShapeGeometry& hemisphere = begin(DebugDraw::Shape::Hemisphere);
    appendArc(verts, 1, 0.f, 2.f * kPi, kRingSegments);
    appendArc(verts, 0, 0.f, kPi, kRingSegments / 2);
    appendArc(verts, 2, 0.f, kPi, kRingSegments / 2);
    end(hemisphere);

    DebugShapeGeometry& cylinder = begin(DebugDraw::Shape::CylinderSide);
    for (const auto& xz : {std::array<float, 2>{1.f, 0.f}, std::array<float, 2>{-1.f, 0.f},
                           std::array<float, 2>{0.f, 1.f}, std::array<float, 2>{0.f, -1.f}})
    {
        writeVertex(verts.emplace_back(), xz[0], -1.f, xz[1]);
        writeVertex(verts.emplace_back(), xz[0], 1.f, xz[1]);
    }
    end(cylinder);

    const uint64_t bytes = sizeof(DebugVertexGPUData) * verts.size();
    debugVertsBuffer_ = resources_.createStaticBuffer(bytes, "debugShapeVerts");
    std::span<std::byte> staging = resources_.requestUpload(debugVertsBuffer_, bytes);
    std::memcpy(staging.data(), verts.data(), bytes);
}

void ScenePasses::uploadDebugDraw(const DebugDraw& depthTested, const DebugDraw& overlay)
{
    const std::array<const DebugDraw*, DebugLayerCount> sources{&depthTested, &overlay};
    std::vector<DebugShapeGPUData> shapes;

    for (size_t i = 0; i < DebugLayerCount; ++i)
    {
        DebugLayer& layer = debugLayers_[i];
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
    std::memcpy(resources_.requestUpload(debugShapesBuffer_, bytes).data(), shapes.data(), bytes);
}

void ScenePasses::uploadBillboards(const Billboards& billboards)
{
    const auto& records = billboards.records();
    billboardCount_ = static_cast<uint32_t>(std::min<size_t>(records.size(), kMaxBillboards));
    if (billboardCount_ == 0)
        return;

    std::vector<BillboardGPUData> out(billboardCount_);
    for (uint32_t i = 0; i < billboardCount_; ++i)
    {
        const Billboards::Record& r = records[i];
        BillboardGPUData& g = out[i];
        g.pos_[0] = r.pos_.x();
        g.pos_[1] = r.pos_.y();
        g.pos_[2] = r.pos_.z();
        g.rot_[0] = r.rot_.x();
        g.rot_[1] = r.rot_.y();
        g.rot_[2] = r.rot_.z();
        g.rot_[3] = r.rot_.w();
        g.sizeX_ = r.size_.x();
        g.sizeY_ = r.size_.y();
        g.tint_[0] = r.tint_.x();
        g.tint_[1] = r.tint_.y();
        g.tint_[2] = r.tint_.z();
        g.tint_[3] = r.alpha_;
        g.materialIdx_ = r.materialIdx_;
        g.textureIdx_ = r.textureIdx_;
        g.flags_ = r.flags_;
    }

    const uint64_t bytes = sizeof(BillboardGPUData) * out.size();
    std::memcpy(resources_.requestUpload(billboardBuffer_, bytes).data(), out.data(), bytes);
}

void ScenePasses::recordDebug(VkCommandBuffer cmd, DrawPush push)
{
    for (const DebugLayer& layer : debugLayers_)
    {
        bool bound = false;
        for (size_t kind = 0; kind < DebugDraw::ShapeCount; ++kind)
        {
            const DebugDrawRange& range = layer.shapes_[kind];
            if (range.instanceCount_ == 0)
                continue;

            if (!bound)
            {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layer.pipeline_);
                bound = true;
            }
            push.instanceIndex_ = range.firstInstance_;
            vkCmdPushConstants(cmd, pipelineLayout_,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                               sizeof(push), &push);
            vkCmdDraw(cmd, debugGeometry_[kind].vertexCount_, range.instanceCount_,
                      debugGeometry_[kind].firstVertex_, 0);
        }
    }
}

void ScenePasses::record(VkCommandBuffer cmd, uint32_t frame, uint32_t width, uint32_t height,
                         const SceneRenderArgs& args, Engine& ctx)
{
    auto* reg = args.reg_;
    auto* instanceM = args.instanceManager_;
    if (!reg)
        return;

    EntityHandle cam;
    reg->view<Camera_C, Transform_C>().each(
        [&](entt::entity e, Camera_C& c, Transform_C&)
        {
            if (c.active_)
                cam = {reg, e};
        });
    if (!cam.valid())
        return;
    const auto camID = instanceM->pool<CameraInstance>().getGPUIndex(cam);
    if (!camID.valid())
        return;

    writeFrameSet(frame, args, ctx);

    setViewportYUp(cmd, width, height);

    const VkDescriptorSet sets[2] = {resources_.textureSet(), frameSets_[frame]};
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 2, sets, 0,
                            nullptr);

    DrawPush push{};
    push.cameraIndex_ = camID;
    instanceM->forEachPool(
        [&](auto& pool)
        {
            using InstanceT = typename std::remove_reference_t<decltype(pool)>::InstanceType;
            if constexpr (requires { InstanceT::CountField; })
                push.*InstanceT::CountField = static_cast<uint32_t>(pool.size());
        });

    // ---- Geometry ----
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, geometryPipeline_);

    reg->view<Mesh_C>().each(
        [&](entt::entity e, Mesh_C& meshC)
        {
            if (!meshC.mesh_)
                return;
            auto* mesh = ctx.assetManager_->get(meshC.mesh_);

            const auto id = instanceM->pool<StaticMeshInstance>().getGPUIndex({reg, e});
            if (!id.valid())
                return;

            if (!mesh->isRenderable())
                return;  // mesh incomplet (pas de normales/uv/tangentes)

            // One resolution for the whole mesh: every stream is in the same
            // buffer, only the offsets differ — and they are already stored in
            // binding order.
            const VkBuffer meshBuffer = resources_.bufferFor(mesh->buffer_);
            const std::array<VkBuffer, Mesh::VertexStreams> vertexBuffers{meshBuffer, meshBuffer,
                                                                          meshBuffer, meshBuffer};

            vkCmdBindVertexBuffers(cmd, 0, Mesh::VertexStreams, vertexBuffers.data(),
                                   mesh->streamOffsets_.data());
            vkCmdBindIndexBuffer(cmd, meshBuffer, mesh->streamOffsets_[Mesh::Index],
                                 toVkIndexType(mesh->indexFormat_));

            // Un draw par submesh, l'index de submesh en push constant (le PS
            // y lit le matériau — pas de SV_PrimitiveID sur Metal)
            push.instanceIndex_ = id;
            if (mesh->subMeshCount == 0)
            {
                push.submeshIndex_ = 0;
                vkCmdPushConstants(cmd, pipelineLayout_,
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                   sizeof(push), &push);
                vkCmdDrawIndexed(cmd, mesh->indexCount_, 1, 0, 0, 0);
                return;
            }
            for (uint8_t sub = 0; sub < mesh->subMeshCount; ++sub)
            {
                push.submeshIndex_ = sub;
                vkCmdPushConstants(cmd, pipelineLayout_,
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                   sizeof(push), &push);
                const auto& subMesh = mesh->subMeshes[sub];
                vkCmdDrawIndexed(cmd, subMesh.indexCount, 1, subMesh.indexOffset, 0, 0);
            }
        });

    // ---- Billboards ----
    // Before the sky, like the geometry: they write depth, so the sky's
    // LESS_OR_EQUAL test then leaves their pixels alone.
    if (billboardCount_ > 0)
    {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, billboardPipeline_);
        push.instanceIndex_ = 0;
        vkCmdPushConstants(cmd, pipelineLayout_,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(push), &push);
        vkCmdDraw(cmd, 6, billboardCount_, 0, 0);
    }

    // ---- Sky (plein écran, derrière la scène) ----
    bool hasSkybox = false;
    reg->view<Skybox_C>().each([&](entt::entity, Skybox_C&) { hasSkybox = true; });
    if (hasSkybox)
    {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skyPipeline_);
        push.instanceIndex_ = 0;
        vkCmdPushConstants(cmd, pipelineLayout_,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(push), &push);
        vkCmdDraw(cmd, 3, 1, 0, 0);
    }

    // After the sky: it writes no depth, so it would paint over any wire drawn
    // against the background.
    recordDebug(cmd, push);
}

}  // namespace batap
