#include "VulkanScenePasses.h"

#include "VulkanContext.h"
#include "VulkanPipelines.h"
#include "VulkanResources.h"

#include "Assets/AssetManager.h"
#include "Components/Camera_C.h"
#include "Components/Transform_C.h"
#include "DebugUtils.h"
#include "Engine.h"
#include "Instance/InstanceManager.h"
#include "Paths.h"

#include <algorithm>
#include <array>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

namespace batap
{
namespace
{

VkDescriptorSetLayout createFrameSetLayout(VkDevice device)
{
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

    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &layout) != VK_SUCCESS)
        throw std::runtime_error("ScenePasses : frame set layout");
    return layout;
}

VkDescriptorPool createFramePool(VkDevice device)
{
    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                  FrameSetBindingCount * FramesInFlight};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = FramesInFlight;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;

    VkDescriptorPool pool = VK_NULL_HANDLE;
    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool) != VK_SUCCESS)
        throw std::runtime_error("ScenePasses : frame pool");
    return pool;
}

std::vector<VkDescriptorSet> allocFrameSets(VkDevice device, VkDescriptorPool pool,
                                            VkDescriptorSetLayout layout)
{
    std::vector<VkDescriptorSetLayout> layouts(FramesInFlight, layout);
    VkDescriptorSetAllocateInfo setInfo{};
    setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    setInfo.descriptorPool = pool;
    setInfo.descriptorSetCount = FramesInFlight;
    setInfo.pSetLayouts = layouts.data();

    std::vector<VkDescriptorSet> sets(FramesInFlight);
    if (vkAllocateDescriptorSets(device, &setInfo, sets.data()) != VK_SUCCESS)
        throw std::runtime_error("ScenePasses : frame sets");
    return sets;
}

VkPipelineLayout createPipelineLayout(VkDevice device, VkDescriptorSetLayout textureSet,
                                      VkDescriptorSetLayout frameSet)
{
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.size = sizeof(DrawPush);

    const VkDescriptorSetLayout setLayouts[2] = {textureSet, frameSet};
    VkPipelineLayoutCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    info.setLayoutCount = 2;
    info.pSetLayouts = setLayouts;
    info.pushConstantRangeCount = 1;
    info.pPushConstantRanges = &pushRange;

    VkPipelineLayout layout = VK_NULL_HANDLE;
    if (vkCreatePipelineLayout(device, &info, nullptr, &layout) != VK_SUCCESS)
        throw std::runtime_error("ScenePasses : pipeline layout");
    return layout;
}

}  // namespace

// Declaration order matters: a pass captures the pipeline layout by value, so
// the layout must already be built when the passes are initialised.
ScenePasses::ScenePasses(VulkanContext& ctx, ResourceManager& resources, VkFormat colorFormat,
                         VkFormat depthFormat)
    : ctx_(ctx),
      resources_(resources),
      frameSetLayout_(createFrameSetLayout(ctx.device_)),
      framePool_(createFramePool(ctx.device_)),
      frameSets_(allocFrameSets(ctx.device_, framePool_, frameSetLayout_)),
      pipelineLayout_(
          createPipelineLayout(ctx.device_, resources.textureSetLayout(), frameSetLayout_)),
      geometry_(PassSetup{ctx, resources, pipelineLayout_, colorFormat, depthFormat}),
      billboards_(PassSetup{ctx, resources, pipelineLayout_, colorFormat, depthFormat}),
      sky_(PassSetup{ctx, resources, pipelineLayout_, colorFormat, depthFormat}),
      debug_(PassSetup{ctx, resources, pipelineLayout_, colorFormat, depthFormat})
{
    const std::string shaderDir = resolveEngineFile("shaders", "shaders");

    std::array<std::optional<ShaderModule>, ShaderCount> owned;
    ShaderModules modules{};
    for (size_t i = 0; i < ShaderCount; ++i)
    {
        owned[i].emplace(ctx_.device_, shaderDir + "/" + kShaders[i].name_ + ".spv");
        modules[i] = *owned[i];
    }
    buildPipelines(modules);
}

ScenePasses::~ScenePasses()
{
    vkDestroyPipelineLayout(ctx_.device_, pipelineLayout_, nullptr);
    vkDestroyDescriptorPool(ctx_.device_, framePool_, nullptr);
    vkDestroyDescriptorSetLayout(ctx_.device_, frameSetLayout_, nullptr);
}

void ScenePasses::buildPipelines(const ShaderModules& modules)
{
    geometry_.buildPipelines(modules);
    billboards_.buildPipelines(modules);
    sky_.buildPipelines(modules);
    debug_.buildPipelines(modules);
}

void ScenePasses::uploadDebugDraw(const DebugDraw& depthTested, const DebugDraw& overlay)
{
    debug_.upload(depthTested, overlay);
}

void ScenePasses::uploadBillboards(const Billboards& billboards)
{
    billboards_.upload(billboards);
}

void ScenePasses::checkHotReload()
{
    namespace fs = std::filesystem;

    // Les sources HLSL de l'arbre (pas les .spv du build) : le hot reload est
    // un outil de dev, il vit la ou on edite.
    const fs::path sourceDir = fs::path(BATAP_ROOT_DIR) / "src/Engine/Shaders";

    // Tout le dossier : un header partage declenche le reload comme une source.
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
    shadersMtime_ = latest;  // meme en cas d'echec : on retentera a la
                             // prochaine sauvegarde, pas a chaque check

    std::array<std::vector<uint8_t>, ShaderCount> spirv;
    for (size_t i = 0; i < ShaderCount; ++i)
    {
        const std::string source =
            (sourceDir / (std::string(kShaders[i].name_) + ".hlsl")).string();
        spirv[i] = ctx_.shaderCompiler_.compile(source, kShaders[i].target_);
        if (spirv[i].empty())
        {
            std::cerr << "[ShaderCompiler] " << kShaders[i].name_
                      << " : compilation echouee, pipelines conservees\n";
            return;
        }
    }

    vkDeviceWaitIdle(ctx_.device_);
    std::array<std::optional<ShaderModule>, ShaderCount> owned;
    ShaderModules modules{};
    for (size_t i = 0; i < ShaderCount; ++i)
    {
        owned[i].emplace(ctx_.device_, spirv[i].data(), spirv[i].size());
        modules[i] = *owned[i];
    }
    buildPipelines(modules);
    std::cout << "[ShaderCompiler] shaders reloaded" << std::endl;
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
    claim(DebugShapeVertsBinding, resources_.bufferFor(debug_.vertsBuffer()));
    claim(DebugShapesBinding, resources_.bufferFor(debug_.shapesBuffer()));
    claim(BillboardsBinding, resources_.bufferFor(billboards_.buffer()));

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

void ScenePasses::record(VkCommandBuffer cmd, uint32_t frame, uint32_t width, uint32_t height,
                         const SceneRenderArgs& args, Engine& ctx)
{
    auto* reg = args.reg_;
    auto* instanceM = args.instanceManager_;
    if (!reg)
        return;

    const EntityHandle cam{reg, args.camera_};
    if (!cam.valid() || !reg->all_of<Camera_C, Transform_C>(args.camera_))
        return;
    const auto camID = instanceM->pool<CameraInstance>().getGPUIndex(cam);
    if (!camID.valid())
        return;

    writeFrameSet(frame, args, ctx);

    setViewportYUp(cmd, width, height);

    const VkDescriptorSet sets[2] = {resources_.textureSet(), frameSets_[frame]};
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 2, sets, 0,
                            nullptr);

    PassContext pass{};
    pass.cmd_ = cmd;
    pass.layout_ = pipelineLayout_;
    pass.push_.cameraIndex_ = camID;
    pass.reg_ = reg;
    pass.instanceManager_ = instanceM;
    pass.assetManager_ = ctx.assetManager_.get();
    instanceM->forEachPool(
        [&](auto& pool)
        {
            using InstanceT = typename std::remove_reference_t<decltype(pool)>::InstanceType;
            if constexpr (requires { InstanceT::CountField; })
                pass.push_.*InstanceT::CountField = static_cast<uint32_t>(pool.size());
        });

    geometry_.record(pass);
    // Before the sky, like the geometry: they write depth, so the sky's
    // LESS_OR_EQUAL test then leaves their pixels alone.
    billboards_.record(pass);
    sky_.record(pass);
    // After the sky: it writes no depth, so it would paint over any wire drawn
    // against the background.
    debug_.record(pass);
}
}  // namespace batap
