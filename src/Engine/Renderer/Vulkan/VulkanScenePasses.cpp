#include "VulkanScenePasses.h"

#include "VulkanContext.h"
#include "VulkanPipelines.h"
#include "VulkanResources.h"

#include "Assets/AssetManager.h"
#include "Components/Camera_C.h"
#include "Components/PointLight_C.h"
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

// Step 3 scaffold: the first casting light, aimed straight down, square 90-degree
// frustum. Step 7 turns this into the six faces of a cube and step 13 derives
// which light gets it.
std::optional<m4f> firstLightViewProj(entt::registry& reg)
{
    for (auto [e, light, trans] : reg.view<PointLight_C, Transform_C>().each())
    {
        if (!light.castShadows_)
            continue;

        const v3f eye = trans.world().translation();
        // A right-handed camera looks along -Z, so looking down -Y puts world +Y
        // on the view's Z.
        const v3f xaxis{1.f, 0.f, 0.f};
        const v3f yaxis{0.f, 0.f, -1.f};
        const v3f zaxis{0.f, 1.f, 0.f};

        m4f view = m4f::Identity();
        view.block<1, 3>(0, 0) = xaxis.transpose();
        view.block<1, 3>(1, 0) = yaxis.transpose();
        view.block<1, 3>(2, 0) = zaxis.transpose();
        view(0, 3) = -xaxis.dot(eye);
        view(1, 3) = -yaxis.dot(eye);
        view(2, 3) = -zaxis.dot(eye);

        const float znear = 0.05f;
        const float zfar = std::max(light.radius_, znear + 1.f);
        const float nf = 1.f / (znear - zfar);

        m4f proj = m4f::Zero();
        proj(0, 0) = 1.f;
        proj(1, 1) = 1.f;
        proj(2, 2) = zfar * nf;
        proj(2, 3) = zfar * znear * nf;
        proj(3, 2) = -1.f;

        return m4f{proj * view};
    }
    return std::nullopt;
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
      shadow_(PassSetup{ctx, resources, pipelineLayout_, colorFormat, depthFormat}),
      geometry_(PassSetup{ctx, resources, pipelineLayout_, colorFormat, depthFormat}),
      billboards_(PassSetup{ctx, resources, pipelineLayout_, colorFormat, depthFormat}),
      sky_(PassSetup{ctx, resources, pipelineLayout_, colorFormat, depthFormat}),
      debug_(PassSetup{ctx, resources, pipelineLayout_, colorFormat, depthFormat})
{
    localAtlas_ = resources_.createTarget(LocalAtlasSize, LocalAtlasSize,
                                          ResourceFormat::D32_FLOAT, "local shadow atlas");

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
    shadow_.buildPipelines(modules);
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
    claim(ShadowsBinding, resources_.bufferFor(shadow_.buffer()));

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

bool ScenePasses::record(VkCommandBuffer cmd, uint32_t frame,
                         const RenderTargets& targets, const SceneRenderArgs& args,
                         Engine& ctx)
{
    auto* reg = args.reg_;
    auto* instanceM = args.instanceManager_;
    if (!reg)
        return false;

    const EntityHandle cam{reg, args.camera_};
    if (!cam.valid() || !reg->all_of<Camera_C, Transform_C>(args.camera_))
        return false;
    const auto camID = instanceM->pool<CameraInstance>().getGPUIndex(cam);
    if (!camID.valid())
        return false;

    writeFrameSet(frame, args, ctx);

    VkRenderingAttachmentInfo color{};
    color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    color.imageView = targets.color_;
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue.color = targets.clearColor_;

    VkRenderingAttachmentInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depth.imageView = targets.depth_;
    depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.clearValue.depthStencil = {1.0f, 0};

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea = {{0, 0}, targets.extent_};
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &color;
    renderingInfo.pDepthAttachment = &depth;
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

    if (const auto shadowViewProj = firstLightViewProj(*reg))
    {
        shadow_.record(pass, resources_.imageFor(localAtlas_), resources_.viewFor(localAtlas_),
                       *shadowViewProj);
    }

    vkCmdBeginRendering(cmd, &renderingInfo);
    setViewportYUp(cmd, targets.extent_.width, targets.extent_.height);

    geometry_.record(pass);
    // Before the sky, like the geometry: they write depth, so the sky's
    // LESS_OR_EQUAL test then leaves their pixels alone.
    billboards_.record(pass);
    sky_.record(pass);
    // After the sky: it writes no depth, so it would paint over any wire drawn
    // against the background.
    debug_.record(pass);

    vkCmdEndRendering(cmd);
    return true;
}
}  // namespace batap
