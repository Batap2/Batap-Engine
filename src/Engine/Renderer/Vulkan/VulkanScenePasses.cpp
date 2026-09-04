#include "VulkanScenePasses.h"

#include "VulkanContext.h"
#include "VulkanFormats.h"
#include "VulkanPipelines.h"
#include "VulkanResources.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include "Assets/AssetManager.h"
#include "Assets/Mesh.h"
#include "Components/Camera_C.h"
#include "Components/Mesh_C.h"
#include "Components/Skybox_C.h"
#include "Components/Transform_C.h"
#include "Instance/InstanceManager.h"
#include "Paths.h"
#include "Shaders/ShaderInterop.h"

namespace batap
{

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
    buildPipelines(vs, ps, skyVS, skyPS);
}

void ScenePasses::buildPipelines(VkShaderModule vs, VkShaderModule ps, VkShaderModule skyVS,
                                 VkShaderModule skyPS)
{
    if (geometryPipeline_)
        vkDestroyPipeline(ctx_.device_, geometryPipeline_, nullptr);
    if (skyPipeline_)
        vkDestroyPipeline(ctx_.device_, skyPipeline_, nullptr);

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
    static constexpr std::array<Stage, 4> stages = {{
        {"VertexShader.hlsl", "vs_6_6"},
        {"PixelShader.hlsl", "ps_6_6"},
        {"SkyVS.hlsl", "vs_6_6"},
        {"SkyPS.hlsl", "ps_6_6"},
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

    std::array<std::vector<uint8_t>, 4> spirv;
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
    buildPipelines(vs, ps, skyVS, skyPS);
    std::cout << "[ShaderCompiler] shaders reloaded" << std::endl;
}

ScenePasses::~ScenePasses()
{
    vkDestroyPipeline(ctx_.device_, geometryPipeline_, nullptr);
    vkDestroyPipeline(ctx_.device_, skyPipeline_, nullptr);
    vkDestroyPipelineLayout(ctx_.device_, pipelineLayout_, nullptr);
    vkDestroyDescriptorPool(ctx_.device_, framePool_, nullptr);
    vkDestroyDescriptorSetLayout(ctx_.device_, frameSetLayout_, nullptr);
}

void ScenePasses::writeFrameSet(uint32_t frame, const SceneRenderArgs& args, Engine& ctx)
{
    auto* instanceM = args.instanceManager_;

    std::array<VkBuffer, FrameSetBindingCount> buffers{};
    instanceM->forEachPool(
        [&](auto& pool)
        {
            using InstanceT = typename std::remove_reference_t<decltype(pool)>::InstanceType;
            buffers[InstanceT::Binding] = resources_.bufferFor(pool.instancePoolHandle_);
        });
    buffers[MaterialsBinding] =
        resources_.bufferFor(ctx.assetManager_->getGPUArena<Material>()->bufferHandle());

    std::array<VkDescriptorBufferInfo, FrameSetBindingCount> bufferInfos{};
    std::array<VkWriteDescriptorSet, FrameSetBindingCount> writes{};
    for (uint32_t i = 0; i < FrameSetBindingCount; ++i)
    {
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

    // ---- Sky (plein écran, derrière la scène) ----
    bool hasSkybox = false;
    reg->view<Skybox_C>().each([&](entt::entity, Skybox_C&) { hasSkybox = true; });
    if (!hasSkybox)
        return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skyPipeline_);
    push.instanceIndex_ = 0;
    vkCmdPushConstants(cmd, pipelineLayout_,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push),
                       &push);
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

}  // namespace batap
