#pragma once

#include "Renderer/Device/ShaderLibrary.h"
#include "Renderer/Draws/DrawContext.h"
#include "Renderer/Graph/RenderGraph.h"

#include <functional>
#include <vector>

namespace batap
{
struct SkyPrefilter
{
    explicit SkyPrefilter(const DrawSetup& setup);
    ~SkyPrefilter();

    SkyPrefilter(const SkyPrefilter&) = delete;
    SkyPrefilter& operator=(const SkyPrefilter&) = delete;

    void buildPipelines(const ShaderModules& modules);
    void createImages(RenderGraph& graph, bool hasSky);
    void addPasses(RenderGraph& graph, const DrawContext& draw,
                   const std::function<void(GraphPass&)>& readsLighting);
    const std::vector<GraphImage>& levels() const { return levels_; }

   private:
    DrawSetup setup_;
    VkPipeline equirect_ = VK_NULL_HANDLE;
    VkPipeline prefilter_ = VK_NULL_HANDLE;
    std::vector<GraphImage> levels_;
};
}  // namespace batap
