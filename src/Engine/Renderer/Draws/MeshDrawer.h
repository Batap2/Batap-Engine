#pragma once

#include "Renderer/Device/ShaderLibrary.h"
#include "Renderer/Draws/DrawContext.h"

#include <cstdint>

namespace batap
{
struct MeshDrawer
{
    explicit MeshDrawer(const DrawSetup& setup) : setup_(setup) {}
    ~MeshDrawer();

    MeshDrawer(const MeshDrawer&) = delete;
    MeshDrawer& operator=(const MeshDrawer&) = delete;

    void buildPipelines(const ShaderModules& modules);
    void record(const DrawContext& draw);

   private:
    DrawSetup setup_;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipeline rtPipeline_ = VK_NULL_HANDLE;
};

void recordMeshDraws(const DrawContext& draw, ResourceManager& resources, uint32_t streams,
                     bool castersOnly);
}  // namespace batap
