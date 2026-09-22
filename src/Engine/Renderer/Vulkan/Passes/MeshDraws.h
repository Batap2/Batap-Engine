#pragma once

#include <cstdint>

namespace batap
{
struct PassContext;
struct ResourceManager;

// `streams` is a count in Mesh::Stream order, so only a prefix: 1 is Position
// alone, Mesh::VertexStreams is all four. The caller binds the pipeline and the
// viewport; the loop pushes pass.push_ with instanceIndex_ and submeshIndex_
// filled in.
void recordMeshDraws(const PassContext& pass, ResourceManager& resources, uint32_t streams);
}  // namespace batap
