#pragma once

#include "Renderer/ResourceFormatWrapper.h"
#include "Handles.h"

namespace rayvox
{
struct Mesh
{
    GPUMeshViewHandle _indexBuffer;
    GPUMeshViewHandle _vertexBuffer;
    GPUMeshViewHandle _normalBuffer;
    GPUMeshViewHandle _tangeantBuffer;
    GPUMeshViewHandle _uv0Buffer;

    ResourceFormat _indexFormat = ResourceFormat::R32_UINT;

    uint32_t _vertexCount = 0;
    uint32_t _indexCount = 0;
};
}  // namespace rayvox
