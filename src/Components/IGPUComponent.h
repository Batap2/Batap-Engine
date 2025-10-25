#pragma once

#include <cstdint>

namespace rayvox
{
struct ResourceManager;

struct IGPUComponent
{
    virtual ~IGPUComponent() = default;
    virtual void allocateGPUResources(ResourceManager* renderer) = 0;
    virtual void releaseGPUResources(ResourceManager* renderer) = 0;
    virtual void uploadIfDirty(ResourceManager* renderer, uint32_t frameIndex) = 0;
};
}  // namespace rayvox