#include "Systems/Terrain_S.h"

#include "Components/Materials_C.h"
#include "Components/Name_C.h"
#include "Components/Terrain_C.h"
#include "Components/Transform_C.h"
#include "Renderer/TerrainDraws.h"
#include "Systems/Hierarchy_S.h"
#include "Terrain/TerrainRelief.h"
#include "World.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace batap
{
namespace
{

// Until the generation is asynchronous (OBJECTIF_TERRAIN.md step 6).
constexpr size_t kBuildsPerFrame = 64;

std::array<uint32_t, kMaxTerrainStrata> materialsOf(const entt::registry& reg, entt::entity e)
{
    std::array<uint32_t, kMaxTerrainStrata> materials{};
    materials.fill(InvalidGPUIndex);
    if (const Materials_C* mats = reg.try_get<Materials_C>(e))
        for (size_t s = 0; s < materials.size() && s < mats->slots.size(); ++s)
            if (mats->slots[s])
                materials[s] = mats->slots[s].index;
    return materials;
}

}  // namespace

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
        if (reg.valid(it->first) && reg.all_of<Terrain_C, Transform_C>(it->first))
        {
            ++it;
            continue;
        }
        drop(world, it->second);
        it = states_.erase(it);
    }

    // A root camera's local position is its world one and is current at
    // once; the world matrix is only flushed after the game update, so on
    // the first frame it is still the identity.
    std::optional<v3f> cameraWorld;
    if (const entt::entity cam = world.renderCamera(); cam != entt::null && reg.valid(cam))
        if (const Transform_C* ct = reg.try_get<Transform_C>(cam))
            cameraWorld = Hierarchy_S::getParent({&reg, cam}) == entt::null ? ct->pos()
                                                                            : ct->worldPos();

    for (auto [e, terrain, tc] : reg.view<Terrain_C, Transform_C>().each())
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
            state.bounds_.clear();
        }
        const NodeBoundsFn bounds = [&](const ChunkKey& key)
        { return state.bounds_.get(key, relief, grid); };

        ChunkKeySet existing;
        for (const auto& [key, chunk] : state.chunks_)
            existing.insert(key);

        std::vector<ChunkKey> wanted;
        std::optional<v3d> camera;
        if (terrain.oneChunk_)
        {
            const int32_t level = std::clamp(terrain.level_, 0, kMaxTerrainLevel);
            const int32_t n = 1 << level;
            wanted.push_back({std::clamp(terrain.face_, 0, rootCount(grid) - 1), level,
                              std::clamp(terrain.x_, 0, n - 1), std::clamp(terrain.y_, 0, n - 1)});
        }
        else if (cameraWorld)
        {
            camera = (tc.world().inverse() * *cameraWorld).cast<double>();
            LodParams params;
            params.grid_ = grid;
            params.maxLevel_ = levelForCell(grid, static_cast<double>(terrain.finestCell_));
            params.ratio_ = static_cast<double>(terrain.lodRatio_);
            wanted = selectChunks(params, *camera, existing, bounds);
        }
        else
            wanted = rootChunks(grid);
        const ChunkKeySet wantedSet(wanted.begin(), wanted.end());

        std::vector<ChunkKey> missing;
        for (const ChunkKey& key : wanted)
            if (!existing.contains(key))
                missing.push_back(key);
        if (camera)
            std::sort(missing.begin(), missing.end(), [&](const ChunkKey& a, const ChunkKey& b)
                      { return distanceTo(bounds(a), *camera) < distanceTo(bounds(b), *camera); });
        if (missing.size() > kBuildsPerFrame)
            missing.resize(kBuildsPerFrame);

        v3d lastCenter = v3d::Zero();
        const auto start = std::chrono::steady_clock::now();
        for (const ChunkKey& key : missing)
        {
            ChunkMesh mesh = buildChunk(relief, grid, key);
            lastCenter = mesh.center_;

            TerrainDraws::Mesh gpu;
            gpu.vertices_ = packVertices(mesh);
            gpu.indices_ = std::move(mesh.indices_);
            gpu.ranges_ = mesh.ranges_;
            gpu.cells_ = static_cast<uint32_t>(grid.cells_);

            Chunk& chunk = state.chunks_[key];
            chunk.mesh_ = draws.add(std::move(gpu));
            chunk.center_ = mesh.center_;
            existing.insert(key);
        }
        const double buildMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                .count();

        // A chunk goes once what replaces it is there: the ancestor it merges
        // into, or every leaf it splits into. Until then both show.
        std::vector<ChunkKey> leaving;
        for (const auto& [key, chunk] : state.chunks_)
        {
            if (wantedSet.contains(key))
                continue;
            bool covered = false;
            bool splits = false;
            bool allLeaves = true;
            for (const ChunkKey& w : wanted)
            {
                if (isAncestor(w, key) && existing.contains(w))
                {
                    covered = true;
                    break;
                }
                if (isAncestor(key, w))
                {
                    splits = true;
                    allLeaves = allLeaves && existing.contains(w);
                }
            }
            if (covered || (splits && allLeaves) || terrain.oneChunk_)
                leaving.push_back(key);
        }
        for (const ChunkKey& key : leaving)
        {
            draws.remove(state.chunks_[key].mesh_);
            state.chunks_.erase(key);
        }

        // Mixed levels only come from the quadtree. Which borders meet a
        // coarser chunk changes with what is shown, so only then.
        const bool morphs = camera.has_value();
        if (!missing.empty() || !leaving.empty())
        {
            ChunkKeySet shown;
            for (const auto& [key, chunk] : state.chunks_)
                shown.insert(key);
            for (auto& [key, chunk] : state.chunks_)
                chunk.force_ = morphs ? morphForce(grid, key, shown) : 0u;
        }
        const double ratio = static_cast<double>(terrain.lodRatio_);
        const std::array<uint32_t, kMaxTerrainStrata> materials = materialsOf(reg, e);
        for (auto& [key, chunk] : state.chunks_)
        {
            TerrainDraws::Params params;
            params.materials_ = materials;
            params.force_ = chunk.force_;
            params.morphs_ = morphs && key.level_ > 0;
            if (params.morphs_)
            {
                params.morphStart_ = static_cast<float>(morphStart(grid, key.level_, ratio));
                params.morphEnd_ = static_cast<float>(morphEnd(grid, key.level_, ratio));
            }
            if (params != chunk.params_)
            {
                draws.setParams(chunk.mesh_, params);
                chunk.params_ = params;
            }
        }

        if (!missing.empty() || !leaving.empty())
        {
            const Name_C* ownerName = owner.try_get<Name_C>();
            std::cout << "[Terrain] " << (ownerName ? ownerName->name_ : std::string{"terrain"})
                      << ": +" << missing.size() << " -" << leaving.size() << " chunks, now "
                      << state.chunks_.size() << ", " << buildMs << " ms";
            if (missing.size() == 1)
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

    // The render camera's position as the GPU gets it (CameraGPUData::pos_).
    v3d camera = v3d::Zero();
    if (const entt::entity cam = world.renderCamera(); cam != entt::null)
        camera = reg.get<Transform_C>(cam).world().translation().cast<double>();

    for (const auto& [e, state] : states_)
    {
        const Transform_C* tc = reg.valid(e) ? reg.try_get<Transform_C>(e) : nullptr;
        if (!tc)
            continue;
        // Centre minus camera in double, then float: small near the camera,
        // where two chunks must agree on the vertices they share. In world
        // coordinates a float is a millimetre wide 10 km out, and each chunk
        // would round its own centre.
        const transform& ownerWorld = tc->world();
        const m3d linear = ownerWorld.linear().cast<double>();
        const v3d offset = ownerWorld.translation().cast<double>() - camera;
        for (const auto& [key, chunk] : state.chunks_)
        {
            m4f m = m4f::Identity();
            m.topLeftCorner<3, 3>() = ownerWorld.linear();
            m.topRightCorner<3, 1>() = (linear * chunk.center_ + offset).cast<float>();
            draws.place(chunk.mesh_, m);
        }
    }
}

}  // namespace batap
