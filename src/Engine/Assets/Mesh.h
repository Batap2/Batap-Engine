#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include "Handles.h"
#include "Renderer/ResourceFormat.h"

namespace batap
{

struct SubMesh
{
    uint32_t indexOffset = 0;
    uint32_t indexCount  = 0;
};

struct Mesh
{
    GPUMeshViewHandle indexBuffer_;
    GPUMeshViewHandle vertexBuffer_;
    GPUMeshViewHandle normalBuffer_;
    GPUMeshViewHandle tangeantBuffer_;
    GPUMeshViewHandle uv0Buffer_;

    ResourceFormat indexFormat_ = ResourceFormat::R32_UINT;

    uint32_t vertexCount_ = 0;
    uint32_t indexCount_  = 0;

    std::array<SubMesh, 8> subMeshes{};
    uint8_t                subMeshCount = 0;
};

}  // namespace batap
