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

void TerrainDraws::setParams(uint64_t mesh, const Params& params)
{
    const auto it = std::find_if(added_.begin(), added_.end(),
                                 [&](const auto& a) { return a.first == mesh; });
    if (it != added_.end())
        it->second.params_ = params;
    else
        paramChanges_.emplace_back(mesh, params);
}

void TerrainDraws::place(uint64_t mesh, const m4f& world)
{
    placed_.emplace_back(mesh, world);
}

}  // namespace batap
