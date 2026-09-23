#pragma once

#include "EigenTypes.h"
#include "Renderer/Vulkan/Passes/Pass.h"
#include "Renderer/Vulkan/Passes/ShaderCatalog.h"

namespace batap
{
struct ShadowPass
{
    explicit ShadowPass(const PassSetup& setup) : setup_(setup) {}
    ~ShadowPass();

    ShadowPass(const ShadowPass&) = delete;
    ShadowPass& operator=(const ShadowPass&) = delete;

    void buildPipelines(const ShaderModules& modules);

    // Opens its own rendering scope: it writes the atlas the scene pass reads,
    // so it cannot sit inside the scene's.
    void record(const PassContext& pass, VkImage atlas, VkImageView atlasView,
                const m4f& viewProj);

   private:
    PassSetup setup_;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
};
}  // namespace batap
