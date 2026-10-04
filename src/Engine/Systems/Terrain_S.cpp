#include "Systems/Terrain_S.h"

#include "Components/Materials_C.h"
#include "Components/Mesh_C.h"
#include "Components/Name_C.h"
#include "Components/Terrain_C.h"
#include "Components/Transform_C.h"
#include "Renderer/DebugDraw.h"
#include "Renderer/TerrainDraws.h"
#include "Systems/Hierarchy_S.h"
#include "Terrain/TerrainRelief.h"
#include "UI/ScreenSpace.h"
#include "World.h"

#include <imgui.h>

#include <algorithm>
#include <bit>
#include <chrono>
#include <iostream>
#include <string>
#include <vector>

namespace batap
{
namespace
{

// Until the generation is asynchronous (OBJECTIF_TERRAIN.md step 6).
constexpr size_t kBuildsPerFrame = 64;

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
        if (reg.valid(it->first) && reg.all_of<Terrain_C>(it->first))
        {
            ++it;
            continue;
        }
        drop(world, it->second);
        it = states_.erase(it);
    }

    // A root camera's local position is its world one and is current at
    // once; the world matrix is only flushed after the game update, so on
    // the first frame it is still the identity, the centre of the astre.
    std::optional<v3f> cameraWorld;
    if (const entt::entity cam = world.renderCamera(); cam != entt::null && reg.valid(cam))
        if (const Transform_C* ct = reg.try_get<Transform_C>(cam))
            cameraWorld = Hierarchy_S::getParent({&reg, cam}) == entt::null ? ct->pos()
                                                                            : ct->worldPos();

    for (auto [e, terrain] : reg.view<Terrain_C>().each())
    {
        EntityHandle astre{&reg, e};
        const int32_t cells = clampChunkCells(terrain.cells_);
        const TerrainSource::Ground ground = source.ground(world, e, cells);
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
        const TerrainGrid grid{terrain.shape_, ground.size_, cells};
        const Transform_C* astreTc = astre.try_get<Transform_C>();

        // The source knows nothing of the shape, which moves every vertex too.
        const uint64_t settings =
            ground.settings_ ^
            (static_cast<uint64_t>(grid.shape_) + 1) * 0x9E3779B97F4A7C15ull ^
            std::bit_cast<uint64_t>(grid.size_) * 0xC2B2AE3D27D4EB4Full;
        State& state = states_[e];
        if (state.settings_ != settings)
        {
            drop(world, state);
            state.settings_ = settings;
            state.bounds_.clear();
        }
        const NodeBoundsFn bounds = [&](const ChunkKey& key)
        { return state.bounds_.get(key, relief, grid); };

        ChunkKeySet existing;
        for (const auto& [key, chunk] : state.chunks_)
            existing.insert(key);

        LodParams params;
        params.grid_ = grid;
        params.maxLevel_ = levelForCell(grid, static_cast<double>(terrain.finestCell_));
        params.ratio_ = static_cast<double>(terrain.lodRatio_);

        std::vector<ChunkKey> wanted;
        std::optional<v3d> camera;
        if (terrain.oneChunk_)
        {
            const int32_t level = std::clamp(terrain.level_, 0, kMaxTerrainLevel);
            const int32_t n = 1 << level;
            wanted.push_back({std::clamp(terrain.face_, 0, rootCount(grid) - 1), level,
                              std::clamp(terrain.x_, 0, n - 1), std::clamp(terrain.y_, 0, n - 1)});
        }
        else if (terrain.lodRatio_ <= 0.f || !cameraWorld || !astreTc)
        {
            // Fixed level, capped: level 4 is already 1536 chunks.
            wanted = allChunksAt(grid, std::clamp(terrain.level_, 0, 4));
        }
        else
        {
            camera = (astreTc->world().inverse() * *cameraWorld).cast<double>();
            wanted = selectChunks(params, *camera, existing, bounds);
        }
        const ChunkKeySet wantedSet(wanted.begin(), wanted.end());

        // The chunks stand in for the astre's own mesh, which the collider
        // still reads (CelestialBody_S) until OBJECTIF_TERRAIN.md step 8.
        if (Mesh_C* own = astre.try_get<Mesh_C>())
            own->visible_ = false;

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
            gpu.cells_ = static_cast<uint32_t>(cells);
            gpu.vertices_ = packVertices(mesh);
            gpu.indices_ = std::move(mesh.indices_);
            gpu.ranges_ = mesh.ranges_;

            Chunk& chunk = state.chunks_[key];
            chunk.mesh_ = draws.add(std::move(gpu));
            chunk.center_ = mesh.center_;
            chunk.border_ = mesh.border_;
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
            if (covered || (splits && allLeaves))
                leaving.push_back(key);
        }
        for (const ChunkKey& key : leaving)
        {
            draws.remove(state.chunks_[key].mesh_);
            state.chunks_.erase(key);
        }

        // Which borders meet a coarser chunk changes with what is shown, so
        // only then.
        if (!missing.empty() || !leaving.empty())
        {
            ChunkKeySet shown;
            for (const auto& [key, chunk] : state.chunks_)
                shown.insert(key);
            for (auto& [key, chunk] : state.chunks_)
                chunk.force_ = morphForce(grid, key, shown);
        }

        state.grid_ = grid;
        state.ratio_ = params.ratio_;
        state.maxLevel_ = params.maxLevel_;
        // Mixed levels only come from the quadtree; a fixed level needs no
        // morph.
        state.morphs_ = camera.has_value();
        state.built_ = missing.size();
        state.dropped_ = leaving.size();
        state.totalBuilt_ += missing.size();
        if (!missing.empty())
            state.lastBuildMs_ = buildMs;

        if (!missing.empty() || !leaving.empty())
        {
            const Name_C* astreName = astre.try_get<Name_C>();
            std::cout << "[Terrain] t=" << world.time_.simTime_ << " "
                      << (astreName ? astreName->name_ : std::string{"astre"}) << ": +"
                      << missing.size() << " -" << leaving.size() << " chunks, now "
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

    std::optional<v3f> cameraWorld;
    if (const entt::entity cam = world.renderCamera(); cam != entt::null && reg.valid(cam))
        if (const Transform_C* ct = reg.try_get<Transform_C>(cam))
            cameraWorld = ct->worldPos();

    for (const auto& [e, state] : states_)
    {
        if (!reg.valid(e))
            continue;
        const Terrain_C* terrain = reg.try_get<Terrain_C>(e);
        const Transform_C* tc = reg.try_get<Transform_C>(e);
        if (!terrain || !tc)
            continue;
        const transform& astreWorld = tc->world();

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
            c.world_ = (astreWorld * Eigen::Translation3f(chunk.center_.cast<float>())).matrix();
            c.materials_ = materials;
            c.force_ = chunk.force_;
            if (state.morphs_ && key.level_ > 0)
            {
                c.morphStart_ =
                    static_cast<float>(morphStart(state.grid_, key.level_, state.ratio_));
                c.morphEnd_ = static_cast<float>(morphEnd(state.grid_, key.level_, state.ratio_));
                c.flags_ = TerrainMorphs;
            }
        }

        if (terrain->debug_)
        {
            std::optional<v3d> camera;
            if (cameraWorld && state.morphs_)
                camera = (astreWorld.inverse() * *cameraWorld).cast<double>();
            debug(world, e, state, astreWorld, camera);
        }
    }
}

void Terrain_S::debug(World& world, entt::entity astre, const State& state,
                      const transform& astreWorld, const std::optional<v3d>& camera)
{
    static const std::array<col3, 8> palette{colors::white, colors::yellow, colors::green,
                                             colors::cyan,  colors::blue,   colors::magenta,
                                             colors::red,   colors::grey};
    DebugDraw& dd = world.debug();
    for (const auto& [key, chunk] : state.chunks_)
    {
        const col3& color = palette[static_cast<size_t>(key.level_) % palette.size()];
        const bool morphs = camera && key.level_ > 0;
        const double start = morphs ? morphStart(state.grid_, key.level_, state.ratio_) : 0.0;
        const double end = morphs ? morphEnd(state.grid_, key.level_, state.ratio_) : 0.0;
        std::array<v3f, 16> points{};
        for (size_t k = 0; k < points.size(); ++k)
        {
            const BorderPoint& bp = chunk.border_[k];
            // Even vertices: the morph never asks for a neighbour.
            const MorphFetch at = [&](int32_t, int32_t)
            {
                return MorphPoint{chunk.center_ + bp.morph_[0].cast<double>(),
                                  chunk.center_ + bp.morph_[1].cast<double>(),
                                  chunk.center_ + bp.morph_[2].cast<double>()};
            };
            const v3d p = morphs ? morphVertex(state.grid_.cells_, bp.i_, bp.j_, chunk.force_,
                                               start, end, at, *camera)
                                 : at(bp.i_, bp.j_).position_;
            points[k] = (p + upAt(state.grid_, p) * static_cast<double>(bp.lift_)).cast<float>();
        }
        for (size_t k = 0; k < points.size(); ++k)
            dd.line(astreWorld * points[k], astreWorld * points[(k + 1) % points.size()], color);
    }

    const auto* name = world.registry_.try_get<Name_C>(astre);
    const size_t chunks = state.chunks_.size();
    const auto cells = static_cast<size_t>(state.grid_.cells_);
    const size_t triangles = chunks * cells * cells * 2;
    const size_t vertices = chunks * (cells + 1) * (cells + 1);
    const double gpuMb = static_cast<double>(vertices * sizeof(TerrainVertexGPUData) +
                                             triangles * 3 * sizeof(uint16_t)) /
                         1048576.0;

    const ViewRect view = world.viewRect();
    const v2f corner = toPoints((view.origin_ + v2i{view.size_.x(), 0}).cast<float>());
    ImGui::SetNextWindowPos({corner.x() - 10.f, corner.y() + 10.f}, ImGuiCond_Always, {1.f, 0.f});
    ImGui::Begin(("Terrain " + (name ? name->name_ : std::string{"astre"})).c_str(), nullptr,
                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    ImGui::Text("%zu chunks, max level %d, ratio %.1f, %d cells", chunks, state.maxLevel_,
                state.ratio_, state.grid_.cells_);
    ImGui::Text("%zu k triangles, %.0f MB, %zu meshes on the GPU", triangles / 1000, gpuMb,
                world.terrainDraws().live_);
    ImGui::Text("+%zu -%zu this frame, %zu built in all, last %.1f ms", state.built_,
                state.dropped_, state.totalBuilt_, state.lastBuildMs_);
    ImGui::End();
}

}  // namespace batap
