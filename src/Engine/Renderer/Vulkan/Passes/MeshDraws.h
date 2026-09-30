#pragma once

#include <cstdint>

namespace batap
{
struct PassContext;
struct ResourceManager;

// `streams`: a prefix of Mesh::Stream order, 1 = Position alone. The caller
// binds the pipeline and the viewport.
void recordMeshDraws(const PassContext& pass, ResourceManager& resources, uint32_t streams);
}  // namespace batap
