#pragma once

#include "Renderer/ResourceFormatWrapper.h"
#include "Handles.h"

namespace rayvox
{
struct Mesh
{
    GPUMeshViewHandle indexBuffer;
    GPUMeshViewHandle posBuffer;
    GPUMeshViewHandle nrmBuffer;
    GPUMeshViewHandle tanBuffer;
    GPUMeshViewHandle uv0Buffer;

    ResourceFormat indexFormat = ResourceFormat::R32_UINT;

    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
};
}  // namespace rayvox
