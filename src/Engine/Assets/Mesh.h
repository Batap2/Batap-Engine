#pragma once

#include <array>
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
    // Order is the vertex binding order of the geometry pipeline, so the first
    // VertexStreams offsets go straight to vkCmdBindVertexBuffers. Index is
    // last for that reason.
    enum Stream : uint8_t
    {
        Position,
        Normal,
        UV0,
        Tangent,
        Index,
        StreamCount
    };
    static constexpr uint32_t VertexStreams = Index;

    GPUResourceHandle buffer_;
    std::array<uint64_t, StreamCount> streamOffsets_{};

    ResourceFormat indexFormat_ = ResourceFormat::R32_UINT;

    uint32_t vertexCount_ = 0;
    uint32_t indexCount_  = 0;

    std::array<SubMesh, 8> subMeshes{};
    uint8_t                subMeshCount = 0;

    bool isRenderable() const { return buffer_.valid(); }
};

}  // namespace batap
