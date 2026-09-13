#pragma once

#include "Renderer/DebugDraw.h"
#include "Shaders/ShaderInterop.h"

#include <array>
#include <cstdint>
#include <vector>

namespace batap
{
// Which slice of the shared unit-wireframe buffer one shape occupies.
struct DebugShapeSlice
{
    uint32_t firstVertex_ = 0;
    uint32_t vertexCount_ = 0;
};

std::vector<DebugVertexGPUData> buildDebugWireframes(
    std::array<DebugShapeSlice, DebugDraw::ShapeCount>& slices);
}  // namespace batap
