#include "Systems/Terrain_S.h"

#include "Components/Character_C.h"
#include "Components/Materials_C.h"
#include "Components/Name_C.h"
#include "Components/RigidBody_C.h"
#include "Components/Terrain_C.h"
#include "Components/Transform_C.h"
#include "Physics/JoltConvert.h"
#include "Physics/MeshCollider.h"
#include "Physics/PhysicsWorld.h"
#include "Renderer/TerrainDraws.h"
#include "Systems/Hierarchy_S.h"
#include "Terrain/TerrainRelief.h"
#include "UI/ScreenSpace.h"
#include "World.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <optional>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

namespace batap
{
namespace
{

// A few milliseconds of copies at most: 64 chunks of 32 cells.
constexpr uint64_t kUploadBytesPerFrame = uint64_t{4} << 20;
// Per terrain, the chunks out of the quadtree kept on the GPU in case it
// comes back.
constexpr uint64_t kCacheBytes = uint64_t{64} << 20;
// Ground is built where a mover may be within a second, and at once under
// one that would otherwise fall through.
constexpr double kColliderReach = 10.0;
constexpr double kColliderLead = 1.0;
constexpr double kColliderAtOnce = 3.0;

int32_t builderThreads()
{
    return std::max(1, static_cast<int32_t>(std::thread::hardware_concurrency()) / 4);
}

uint64_t chunkBytes(int32_t cells)
{
    const auto c = static_cast<uint64_t>(cells);
    return (c + 1) * (c + 1) * sizeof(TerrainVertexGPUData) + c * c * 6 * sizeof(uint16_t);
}

// A root's local pose is its world one and is current at once; the world
// matrix is only flushed after the game update, so on the first frame it is
// still the identity.
v3f worldPositionOf(entt::registry& reg, entt::entity e)
{
    const Transform_C& tc = reg.get<Transform_C>(e);
    return Hierarchy_S::getParent({&reg, e}) == entt::null ? tc.pos() : tc.worldPos();
}

quatf worldRotationOf(entt::registry& reg, entt::entity e)
{
    const Transform_C& tc = reg.get<Transform_C>(e);
    return Hierarchy_S::getParent({&reg, e}) == entt::null ? tc.rot()
                                                           : quatf{tc.world().rotation()};
}

v3f kinematicVelocityOf(const entt::registry& reg, entt::entity e)
{
    const RigidBody_C* rb = reg.try_get<RigidBody_C>(e);
    return rb && rb->motion_ == RigidBody_C::Motion::Kinematic && rb->kinematicVelocitySet_
               ? rb->kinematicVelocity_
               : v3f::Zero();
}

TerrainColliders::Surface surfaceOf(const entt::registry& reg, entt::entity owner)
{
    TerrainColliders::Surface surface;
    surface.userData_ = static_cast<uint64_t>(entt::to_integral(owner)) + 1u;
    if (const RigidBody_C* rb = reg.try_get<RigidBody_C>(owner))
    {
        surface.friction_ = rb->friction_;
        surface.restitution_ = rb->restitution_;
    }
    return surface;
}

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

Terrain_S::Terrain_S() : builder_(builderThreads()) {}

void Terrain_S::drop(World& world, State& state)
{
    TerrainDraws& draws = world.terrainDraws();
    for (const auto& [key, chunk] : state.chunks_)
        draws.remove(chunk.mesh_);
    state.chunks_.clear();
    state.colliders_.clear(world.physics());
    state.wanted_.clear();
    state.shown_.clear();
    state.wishes_.clear();
}

void Terrain_S::clear(World& world)
{
    builder_.cancel();
    for (auto& [e, state] : states_)
        drop(world, state);
    states_.clear();
}

void Terrain_S::fixedUpdate(World& world, float dt)
{
    auto& reg = world.registry_;
    for (auto& [e, state] : states_)
    {
        if (!reg.valid(e) || !reg.all_of<Transform_C>(e))
            continue;
        const v3f velocity = kinematicVelocityOf(reg, e);
        state.colliders_.place(world.physics(),
                               worldPositionOf(reg, e).cast<double>() -
                                   (velocity * dt).cast<double>(),
                               worldRotationOf(reg, e).cast<double>(), velocity, surfaceOf(reg, e));
    }
}

void Terrain_S::sync(World& world, TerrainSource& source)
{
    auto& reg = world.registry_;
    TerrainDraws& draws = world.terrainDraws();
    ++syncs_;

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

    std::optional<v3f> cameraWorld;
    if (const entt::entity cam = world.renderCamera();
        cam != entt::null && reg.valid(cam) && reg.all_of<Transform_C>(cam))
        cameraWorld = worldPositionOf(reg, cam);

    JPH::BodyInterface& bodies = world.physics().bodies();
    std::vector<Mover> movers;
    for (auto [e, rb, tc] : reg.view<RigidBody_C, Transform_C>().each())
    {
        if (!rb.active_ || rb.motion_ != RigidBody_C::Motion::Dynamic)
            continue;
        if (rb.bodyId_ == kInvalidBodyId)
            movers.push_back({worldPositionOf(reg, e).cast<double>(), 0.0});
        else
        {
            const JPH::BodyID id{rb.bodyId_};
            movers.push_back({toEigen(bodies.GetPosition(id)).cast<double>(),
                              static_cast<double>(toEigen(bodies.GetLinearVelocity(id)).norm())});
        }
    }
    for (auto [e, character, tc] : reg.view<Character_C, Transform_C>().each())
        movers.push_back({worldPositionOf(reg, e).cast<double>(),
                          static_cast<double>(character.velocity_.norm())});

    std::vector<ChunkBuilder::Result> arrivals = builder_.take(kUploadBytesPerFrame);
    const bool meshesArrived = std::any_of(arrivals.begin(), arrivals.end(),
                                           [](const ChunkBuilder::Result& r)
                                           { return !r.collider_; });
    std::vector<Wish> wishes;

    for (auto [e, terrain, tc] : reg.view<Terrain_C, Transform_C>().each())
    {
        const auto start = std::chrono::steady_clock::now();
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
            wanted = selectChunks(params, *camera, state.shown_, bounds);
        }
        else
            wanted = rootChunks(grid);
        // Mixed levels only come from the quadtree.
        const bool nested = camera.has_value();

        state.arrived_ = 0;
        state.evicted_ = 0;
        const bool moved = wanted != state.wanted_;
        ChunkKeySet tree;
        if (moved || meshesArrived)
            tree = nested ? quadtreeOf(wanted) : ChunkKeySet(wanted.begin(), wanted.end());

        for (ChunkBuilder::Result& r : arrivals)
        {
            if (r.collider_ || r.owner_ != e || r.fingerprint_ != fingerprint ||
                !tree.contains(r.key_) || state.chunks_.contains(r.key_))
                continue;
            Chunk& chunk = state.chunks_[r.key_];
            chunk.center_ = r.center_;
            chunk.used_ = syncs_;
            chunk.mesh_ = draws.add(std::move(r.mesh_));
            ++state.arrived_;
            ++state.rateBuilt_;
            state.rateBuildMs_ += r.ms_;
        }

        if (moved || state.arrived_ > 0)
        {
            const ChunkTest built = [&](const ChunkKey& key)
            { return state.chunks_.contains(key); };
            std::vector<ChunkKey> shownList;
            std::vector<ChunkKey> frontier;
            if (nested)
            {
                shownList = shownChunks(grid, tree, built);
                frontier = buildFrontier(tree, built);
            }
            else
                for (const ChunkKey& key : wanted)
                    (built(key) ? shownList : frontier).push_back(key);

            ChunkKeySet shown(shownList.begin(), shownList.end());
            std::vector<ChunkKey> changed;
            for (auto& [key, chunk] : state.chunks_)
                if (const bool show = shown.contains(key); chunk.shown_ != show)
                {
                    draws.show(chunk.mesh_, show);
                    chunk.shown_ = show;
                    changed.push_back(key);
                }
            // Which borders meet a coarser chunk changes with what is shown.
            for (const ChunkKey& key : chunksTouching(grid, changed, shown))
                state.chunks_[key].force_ = nested ? morphForce(grid, key, shown) : 0u;
            state.shown_ = std::move(shown);

            state.wishes_.clear();
            for (const ChunkKey& key : frontier)
                state.wishes_.push_back(
                    {camera ? distanceTo(bounds(key), *camera) : 0.0,
                     ChunkBuilder::Request{e, fingerprint, ground.relief_, grid, key}});

            std::vector<std::pair<uint64_t, ChunkKey>> cached;
            for (auto& [key, chunk] : state.chunks_)
                if (tree.contains(key))
                    chunk.used_ = syncs_;
                else
                    cached.emplace_back(chunk.used_, key);
            const uint64_t keep = kCacheBytes / chunkBytes(grid.cells_);
            if (cached.size() > keep)
            {
                const auto oldest = cached.end() - static_cast<std::ptrdiff_t>(keep);
                std::nth_element(cached.begin(), oldest, cached.end(),
                                 [](const auto& a, const auto& b) { return a.first < b.first; });
                for (auto it = cached.begin(); it != oldest; ++it)
                {
                    draws.remove(state.chunks_[it->second].mesh_);
                    state.chunks_.erase(it->second);
                    ++state.evicted_;
                }
            }
            state.wanted_ = std::move(wanted);
        }
        wishes.insert(wishes.end(), state.wishes_.begin(), state.wishes_.end());
        syncColliders(world, e, terrain, ground, grid, state, movers, arrivals, wishes);

        const double ratio = static_cast<double>(terrain.lodRatio_);
        const std::array<uint32_t, kMaxTerrainStrata> materials = materialsOf(reg, e);
        for (const ChunkKey& key : state.shown_)
        {
            Chunk& chunk = state.chunks_[key];
            TerrainDraws::Params params;
            params.materials_ = materials;
            params.force_ = chunk.force_;
            params.outline_ = showChunks_;
            params.morphs_ = nested && key.level_ > 0;
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

        state.grid_ = grid;
        state.ratio_ = ratio;
        state.maxLevel_ = levelForCell(grid, static_cast<double>(terrain.finestCell_));
        state.totalBuilt_ += state.arrived_;
        const auto now = std::chrono::steady_clock::now();
        state.syncMs_ = std::chrono::duration<double, std::milli>(now - start).count();
        if (const double seconds = std::chrono::duration<double>(now - state.rateStart_).count();
            seconds >= 1.0)
        {
            state.chunksPerSecond_ = static_cast<double>(state.rateBuilt_) / seconds;
            state.msPerChunk_ = state.rateBuilt_ > 0
                                    ? state.rateBuildMs_ / static_cast<double>(state.rateBuilt_)
                                    : 0.0;
            state.rateStart_ = now;
            state.rateBuilt_ = 0;
            state.rateBuildMs_ = 0.0;
        }
    }

    // Colliders first: a mover without ground falls through, a missing mesh
    // only leaves its parent showing. Then nearest first; at equal distance,
    // the camera inside both, the coarser.
    std::sort(wishes.begin(), wishes.end(),
              [](const Wish& a, const Wish& b)
              {
                  return std::tuple(!a.request_.collider_, a.distance_, a.request_.key_.level_) <
                         std::tuple(!b.request_.collider_, b.distance_, b.request_.key_.level_);
              });
    std::vector<ChunkBuilder::Request> requests;
    requests.reserve(wishes.size());
    for (Wish& w : wishes)
        requests.push_back(std::move(w.request_));
    builder_.request(std::move(requests));
}

void Terrain_S::syncColliders(World& world, entt::entity owner, const Terrain_C& terrain,
                              const TerrainSource::Ground& ground, const TerrainGrid& grid,
                              State& state, const std::vector<Mover>& movers,
                              std::vector<ChunkBuilder::Result>& arrivals,
                              std::vector<Wish>& wishes)
{
    auto& reg = world.registry_;
    PhysicsWorld& physics = world.physics();

    const v3d origin = worldPositionOf(reg, owner).cast<double>();
    const quatd rotation = worldRotationOf(reg, owner).cast<double>();
    state.colliders_.place(physics, origin, rotation, kinematicVelocityOf(reg, owner),
                           surfaceOf(reg, owner));

    std::vector<Reach> reaches;
    reaches.reserve(movers.size());
    for (const Mover& m : movers)
        reaches.push_back({rotation.conjugate() * (m.position_ - origin),
                           kColliderReach + kColliderLead * m.speed_});
    const NodeBoundsFn bounds = [&](const ChunkKey& key)
    { return state.bounds_.get(key, *ground.relief_, grid); };
    const int32_t level = levelClosestToCell(grid, static_cast<double>(terrain.physicsCell_));
    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    const std::vector<ChunkKey> missing =
        state.colliders_.update(physics, grid, level, reaches, bounds, seconds);

    for (const ChunkBuilder::Result& r : arrivals)
        if (r.collider_ && r.owner_ == owner && r.fingerprint_ == state.fingerprint_ &&
            state.colliders_.wants(r.key_))
            state.colliders_.add(physics, r.key_, r.center_, r.shape_);

    for (const ChunkKey& key : missing)
    {
        if (state.colliders_.has(key))
            continue;
        const Box3d box = bounds(key);
        double nearest = std::numeric_limits<double>::infinity();
        for (const Reach& r : reaches)
            nearest = std::min(nearest, distanceTo(box, r.center_));
        if (nearest < kColliderAtOnce)
        {
            const ChunkSurface s = chunkSurface(*ground.relief_, grid, key);
            state.colliders_.add(physics, key, s.center_,
                                 makeGridMeshShape(s.positions_, grid.cells_));
            ++state.collidersAtOnce_;
            continue;
        }
        ChunkBuilder::Request request{owner, state.fingerprint_, ground.relief_, grid, key};
        request.collider_ = true;
        wishes.push_back({nearest, std::move(request)});
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
        for (const ChunkKey& key : state.shown_)
        {
            const Chunk& chunk = state.chunks_.at(key);
            m4f m = m4f::Identity();
            m.topLeftCorner<3, 3>() = ownerWorld.linear();
            m.topRightCorner<3, 1>() = (linear * chunk.center_ + offset).cast<float>();
            draws.place(chunk.mesh_, m);
        }

        if (showChunks_)
            debug(world, e, state);
    }
}

void Terrain_S::debug(World& world, entt::entity owner, const State& state)
{
    const TerrainDraws::Upload& upload = world.terrainDraws().lastUpload_;
    const auto* name = world.registry_.try_get<Name_C>(owner);
    const size_t shown = state.shown_.size();
    const auto cells = static_cast<size_t>(state.grid_.cells_);
    const size_t triangles = shown * cells * cells * 2;
    const size_t resident = state.chunks_.size();
    const double gpuMb = static_cast<double>(resident * chunkBytes(state.grid_.cells_)) / 1048576.0;

    const ViewRect view = world.viewRect();
    const v2f corner = toPoints((view.origin_ + v2i{view.size_.x(), 0}).cast<float>());
    ImGui::SetNextWindowPos({corner.x() - 10.f, corner.y() + 10.f}, ImGuiCond_Always, {1.f, 0.f});
    ImGui::Begin(("Terrain " + (name ? name->name_ : std::string{"terrain"})).c_str(), nullptr,
                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    ImGui::Text("%zu chunks shown, max level %d, ratio %.1f, %d cells", shown, state.maxLevel_,
                state.ratio_, state.grid_.cells_);
    ImGui::Text("%zu k triangles; on the GPU %zu chunks, %.0f MB, %zu meshes in all",
                triangles / 1000, resident, gpuMb, upload.meshes_);
    ImGui::Text("+%zu -%zu this frame, %zu built in all", state.arrived_, state.evicted_,
                state.totalBuilt_);
    ImGui::Text("%.0f chunks/s, %.2f ms each; %zu queued, %zu under way", state.chunksPerSecond_,
                state.msPerChunk_, builder_.queued(), builder_.underWay());
    const int32_t colliderLevel = state.colliders_.level();
    ImGui::Text("%zu colliders, level %d, %.1f cm cells; %zu built at once",
                state.colliders_.size(), colliderLevel,
                colliderLevel >= 0 ? cellSize(state.grid_, colliderLevel) * 100.0 : 0.0,
                state.collidersAtOnce_);
    ImGui::Text("sync %.2f ms, upload %.1f KB, %.2f ms", state.syncMs_,
                static_cast<double>(upload.bytes_) / 1024.0, upload.ms_);
    ImGui::End();
}

}  // namespace batap
