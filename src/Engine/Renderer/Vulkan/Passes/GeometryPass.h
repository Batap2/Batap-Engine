#pragma once

#include "Renderer/Vulkan/Passes/Pass.h"
#include "Renderer/Vulkan/Passes/ShaderCatalog.h"

namespace batap
{
struct GeometryPass
{
    explicit GeometryPass(const PassSetup& setup) : setup_(setup) {}
    ~GeometryPass();

    GeometryPass(const GeometryPass&) = delete;
    GeometryPass& operator=(const GeometryPass&) = delete;

    void buildPipelines(const ShaderModules& modules);
    void record(const PassContext& pass);

   private:
    PassSetup setup_;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
};
}  // namespace batap
