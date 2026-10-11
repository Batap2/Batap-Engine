#include "Systems/Terrain_S.h"

#include "Components/Materials_C.h"
#include "Components/Name_C.h"
#include "Components/Terrain_C.h"
#include "Components/Transform_C.h"
#include "Renderer/TerrainDraws.h"
#include "Terrain/TerrainRelief.h"
#include "World.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <iostream>
#include <string>
#include <vector>

namespace batap
{

void Terrain_S::drop(World& world, State& state)
{
    TerrainDraws& draws = world.terrainDraws();
    for (const auto& [key, chunk] : state.chunks_)
        draws.remove(chunk.mesh_);
    state.chunks_.clear();
}

void Terrain_S::clear(World& world)
{
    for (auto& [e, state] : states_)
        drop(world, state);
    states_.clear();
}

void Terrain_S::sync(World& world, TerrainSource& source)
{
    auto& reg = world.registry_;
    TerrainDraws& draws = world.terrainDraws();

    for (auto it = states_.begin(); it != states_.end();)
    {
        if (reg.valid(it->first) && reg.all_of<Terrain_C>(it->first))
        {
            ++it;
            continue;
        }
        drop(world, it->second);
        it = states_.erase(it);
    }

    for (auto [e, terrain] : reg.view<Terrain_C>().each())
    {
        EntityHandle owner{&reg, e};
        const TerrainSource::Ground ground = source.ground(world, e);
        if (!ground.relief_)
        {
            if (const auto it = states_.find(e); it != states_.end())
            {
                drop(world, it->second);
                states_.erase(it);
            }
            continue;
        }
        const TerrainRelief& relief = *ground.relief_;
        const TerrainGrid grid{terrain.shape_, ground.size_, clampChunkCells(terrain.cells_)};

        // The source knows nothing of the grid, which moves every vertex too.
        const uint64_t fingerprint =
            ground.fingerprint_ ^ (static_cast<uint64_t>(grid.shape_) + 1) * 0x9E3779B97F4A7C15ull ^
            std::bit_cast<uint64_t>(grid.size_) * 0xC2B2AE3D27D4EB4Full ^
            static_cast<uint64_t>(grid.cells_) * 0x165667B19E3779F9ull;
        State& state = states_[e];
        if (state.fingerprint_ != fingerprint)
        {
            drop(world, state);
            state.fingerprint_ = fingerprint;
        }

        std::vector<ChunkKey> wanted;
        if (terrain.oneChunk_)
        {
            const int32_t level = std::clamp(terrain.level_, 0, kMaxTerrainLevel);
            const int32_t n = 1 << level;
            wanted.push_back({std::clamp(terrain.face_, 0, rootCount(grid) - 1), level,
                              std::clamp(terrain.x_, 0, n - 1), std::clamp(terrain.y_, 0, n - 1)});
        }

        std::vector<ChunkKey> leaving;
        for (const auto& [key, chunk] : state.chunks_)
            if (std::find(wanted.begin(), wanted.end(), key) == wanted.end())
                leaving.push_back(key);
        for (const ChunkKey& key : leaving)
        {
            draws.remove(state.chunks_[key].mesh_);
            state.chunks_.erase(key);
        }

        size_t built = 0;
        v3d lastCenter = v3d::Zero();
        const auto start = std::chrono::steady_clock::now();
        for (const ChunkKey& key : wanted)
        {
            if (state.chunks_.contains(key))
                continue;
            ChunkMesh mesh = buildChunk(relief, grid, key);
            lastCenter = mesh.center_;

            TerrainDraws::Mesh gpu;
            gpu.vertices_ = packVertices(mesh);
            gpu.indices_ = std::move(mesh.indices_);
            gpu.ranges_ = mesh.ranges_;

            Chunk& chunk = state.chunks_[key];
            chunk.mesh_ = draws.add(std::move(gpu));
            chunk.center_ = mesh.center_;
            ++built;
        }
        const double buildMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                .count();

        if (built > 0 || !leaving.empty())
        {
            const Name_C* ownerName = owner.try_get<Name_C>();
            std::cout << "[Terrain] " << (ownerName ? ownerName->name_ : std::string{"terrain"})
                      << ": +" << built << " -" << leaving.size() << " chunks, now "
                      << state.chunks_.size() << ", " << buildMs << " ms";
            if (built == 1)
                std::cout << ", centre (" << lastCenter.x() << ", " << lastCenter.y() << ", "
                          << lastCenter.z() << ")";
            std::cout << std::endl;
        }
    }
}

void Terrain_S::publish(World& world)
{
    auto& reg = world.registry_;
    TerrainDraws& draws = world.terrainDraws();

    for (const auto& [e, state] : states_)
    {
        if (!reg.valid(e))
            continue;
        const Transform_C* tc = reg.try_get<Transform_C>(e);
        if (!tc)
            continue;
        const transform& ownerWorld = tc->world();

        std::array<uint32_t, kMaxTerrainStrata> materials{};
        materials.fill(InvalidGPUIndex);
        if (const Materials_C* mats = reg.try_get<Materials_C>(e))
            for (size_t s = 0; s < materials.size() && s < mats->slots.size(); ++s)
                if (mats->slots[s])
                    materials[s] = mats->slots[s].index;

        for (const auto& [key, chunk] : state.chunks_)
        {
            TerrainDraws::Chunk& c = draws.chunks_.emplace_back();
            c.mesh_ = chunk.mesh_;
            c.world_ = (ownerWorld * Eigen::Translation3f(chunk.center_.cast<float>())).matrix();
            c.materials_ = materials;
        }
    }
}

}  // namespace batap
