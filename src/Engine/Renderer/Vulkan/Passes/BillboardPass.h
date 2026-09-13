#pragma once

#include "Handles.h"
#include "Renderer/Vulkan/Passes/Pass.h"
#include "Renderer/Vulkan/Passes/ShaderCatalog.h"

namespace batap
{
struct Billboards;

struct BillboardPass
{
    explicit BillboardPass(const PassSetup& setup);
    ~BillboardPass();

    BillboardPass(const BillboardPass&) = delete;
    BillboardPass& operator=(const BillboardPass&) = delete;

    void buildPipelines(const ShaderModules& modules);
    void record(const PassContext& pass);

    // Staging is filled during the update and copied by flushUploads at the
    // top of the next render — same contract as the instance pools.
    void upload(const Billboards& billboards);

    GPUResourceHandle buffer() const { return buffer_; }

   private:
    PassSetup setup_;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    GPUResourceHandle buffer_;
    uint32_t count_ = 0;
};
}  // namespace batap
