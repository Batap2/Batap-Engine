#pragma once

#include "Renderer/Vulkan/Passes/Pass.h"
#include "Renderer/Vulkan/Passes/ShaderCatalog.h"

namespace batap
{
struct TonemapPass
{
    explicit TonemapPass(const PassSetup& setup) : setup_(setup) {}
    ~TonemapPass();

    TonemapPass(const TonemapPass&) = delete;
    TonemapPass& operator=(const TonemapPass&) = delete;

    void buildPipelines(const ShaderModules& modules);
    void record(const PassContext& pass);

   private:
    PassSetup setup_;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
};
}  // namespace batap
