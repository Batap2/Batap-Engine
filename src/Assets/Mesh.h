#pragma once

#include "Renderer/includeDX12.h"
#include "Handles.h"

namespace rayvox
{
struct Mesh
{
    GPUHandle posBuffer;
    GPUHandle nrmBuffer;
    GPUHandle tanBuffer;
    GPUHandle uv0Buffer;

    GPUHandle indexBuffer;
    DXGI_FORMAT indexFormat = DXGI_FORMAT_R32_UINT;

    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
};
}  // namespace rayvox
