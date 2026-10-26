#pragma once

#include "Handles.h"
#include "Renderer/Device/ShaderLibrary.h"
#include "Renderer/Draws/DrawContext.h"

namespace batap
{
struct Billboards;

struct BillboardDrawer
{
    explicit BillboardDrawer(const DrawSetup& setup);
    ~BillboardDrawer();

    BillboardDrawer(const BillboardDrawer&) = delete;
    BillboardDrawer& operator=(const BillboardDrawer&) = delete;

    void buildPipelines(const ShaderModules& modules);
    void record(const DrawContext& draw);

    // Staging is filled during the update and copied by flushUploads at the
    // top of the next render — same contract as the instance pools.
    void upload(const Billboards& billboards);

    GPUResourceHandle buffer() const { return buffer_; }

   private:
    DrawSetup setup_;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    GPUResourceHandle buffer_;
    uint32_t count_ = 0;
};
}  // namespace batap
