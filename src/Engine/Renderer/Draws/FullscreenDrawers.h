#pragma once

#include "Renderer/Device/ShaderLibrary.h"
#include "Renderer/Draws/DrawContext.h"

namespace batap
{
struct FullscreenDrawers
{
    FullscreenDrawers(const DrawSetup& hdr, VkFormat outputFormat)
        : setup_(hdr), outputFormat_(outputFormat)
    {}
    ~FullscreenDrawers();

    FullscreenDrawers(const FullscreenDrawers&) = delete;
    FullscreenDrawers& operator=(const FullscreenDrawers&) = delete;

    void buildPipelines(const ShaderModules& modules);
    void recordSky(const DrawContext& draw);
    void recordTonemap(const DrawContext& draw);
    void recordRtFirstHit(const DrawContext& draw);

   private:
    DrawSetup setup_;
    VkFormat outputFormat_ = VK_FORMAT_UNDEFINED;
    VkPipeline sky_ = VK_NULL_HANDLE;
    VkPipeline tonemap_ = VK_NULL_HANDLE;
    VkPipeline rtFirstHit_ = VK_NULL_HANDLE;
};
}  // namespace batap
