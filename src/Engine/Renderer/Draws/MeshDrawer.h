#pragma once

#include "Renderer/Device/ShaderLibrary.h"
#include "Renderer/Draws/DrawContext.h"

#include <cstdint>

namespace batap
{
struct Mesh;
struct Materials_C;

enum class MeshPass : uint8_t
{
    Opaque,
    ShadowCasters,
    Translucent,
};

struct MeshDrawer
{
    explicit MeshDrawer(const DrawSetup& setup) : setup_(setup) {}
    ~MeshDrawer();

    MeshDrawer(const MeshDrawer&) = delete;
    MeshDrawer& operator=(const MeshDrawer&) = delete;

    void buildPipelines(const ShaderModules& modules);
    void recordPrepass(const DrawContext& draw);
    void record(const DrawContext& draw);
    void recordTranslucent(const DrawContext& draw);

   private:
    DrawSetup setup_;
    VkPipeline prepassPipeline_ = VK_NULL_HANDLE;
    VkPipeline reflectPrepassPipeline_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipeline rtPipeline_ = VK_NULL_HANDLE;
    VkPipeline translucentPipeline_ = VK_NULL_HANDLE;
    VkPipeline translucentRtPipeline_ = VK_NULL_HANDLE;
};

bool isTranslucent(const Material& material);
uint32_t translucentSubmeshes(AssetManager& assets, const Mesh& mesh, const Materials_C* materials);
bool hasTranslucentMeshes(entt::registry& reg, AssetManager& assets);

void recordMeshDraws(const DrawContext& draw, ResourceManager& resources, uint32_t streams,
                     MeshPass pass);
}  // namespace batap
