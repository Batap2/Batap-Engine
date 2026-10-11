#include "Renderer/TerrainDraws.h"

#include <algorithm>

namespace batap
{

uint64_t TerrainDraws::add(Mesh mesh)
{
    const uint64_t id = next_++;
    added_.emplace_back(id, std::move(mesh));
    return id;
}

void TerrainDraws::remove(uint64_t mesh)
{
    const auto it = std::find_if(added_.begin(), added_.end(),
                                 [&](const auto& a) { return a.first == mesh; });
    if (it != added_.end())
        added_.erase(it);
    else
        removed_.push_back(mesh);
}

}  // namespace batap
