#pragma once

#include "Renderer/Vulkan/Passes/Pass.h"
#include "Renderer/Vulkan/Passes/ShaderCatalog.h"

namespace batap
{
struct RtDebugPass
{
    explicit RtDebugPass(const PassSetup& setup) : setup_(setup) {}
    ~RtDebugPass();

    RtDebugPass(const RtDebugPass&) = delete;
    RtDebugPass& operator=(const RtDebugPass&) = delete;

    void buildPipelines(const ShaderModules& modules);
    void record(const PassContext& pass);

   private:
    PassSetup setup_;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
};
}  // namespace batap
