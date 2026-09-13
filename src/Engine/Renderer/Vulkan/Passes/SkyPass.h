#pragma once

#include "Renderer/Vulkan/Passes/Pass.h"
#include "Renderer/Vulkan/Passes/ShaderCatalog.h"

namespace batap
{
struct SkyPass
{
    explicit SkyPass(const PassSetup& setup) : setup_(setup) {}
    ~SkyPass();

    SkyPass(const SkyPass&) = delete;
    SkyPass& operator=(const SkyPass&) = delete;

    void buildPipelines(const ShaderModules& modules);
    void record(const PassContext& pass);

   private:
    PassSetup setup_;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
};
}  // namespace batap
