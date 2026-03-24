#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include "Handles.h"
#include "Renderer/ResourceFormatWrapper.h"

namespace batap
{

struct SubMesh
{
    uint32_t indexOffset = 0;
    uint32_t indexCount  = 0;
};

struct Mesh
{
    GPUMeshViewHandle _indexBuffer;
    GPUMeshViewHandle _vertexBuffer;
    GPUMeshViewHandle _normalBuffer;
    GPUMeshViewHandle _tangeantBuffer;
    GPUMeshViewHandle _uv0Buffer;

    ResourceFormat _indexFormat = ResourceFormat::R32_UINT;

    uint32_t _vertexCount = 0;
    uint32_t _indexCount  = 0;

    std::array<SubMesh, 8> subMeshes{};
    uint8_t                subMeshCount = 0;
};

}  // namespace batap
