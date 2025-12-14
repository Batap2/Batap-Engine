#pragma once

#include <DirectX-Headers/include/directx/d3dx12.h>
#include "Handles.h"

namespace rayvox
{
struct Mesh
{
    // Streams (Structure of Arrays)
    GPUHandle posBuffer;  // float3 * vertexCount
    GPUHandle nrmBuffer;  // float3 * vertexCount
    GPUHandle tanBuffer;  // float4 * vertexCount (optional)
    GPUHandle uv0Buffer;  // float2 * vertexCount (optional)

    // Indices
    GPUHandle indexBuffer;
    DXGI_FORMAT indexFormat = DXGI_FORMAT_R32_UINT;

    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
};
}  // namespace rayvox