#pragma once

#include "EigenTypes.h"
#include "Terrain/TerrainLod.h"

#include <Jolt/Jolt.h>

#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace batap
{

struct PhysicsWorld;

struct TerrainColliders
{
    struct Surface
    {
        // Contacts and ray hits resolve to this, as for Physics_S's bodies.
        uint64_t userData_ = 0;
        float friction_ = 0.2f;
        float restitution_ = 0.f;
    };

    // The terrain's frame in the world, and what its ground is made of: the
    // bodies already there follow.
    void place(PhysicsWorld& physics, const v3d& origin, const quatd& rotation, const v3f& velocity,
               const Surface& surface);
    // The nodes of `level` within reach, by the boxes of `bounds`. Returns
    // those that have no body yet; a body out of reach for a while leaves.
    // `seconds` reads any clock.
    std::vector<ChunkKey> update(PhysicsWorld& physics, const TerrainGrid& grid, int32_t level,
                                 const std::vector<Reach>& reaches, const NodeBoundsFn& bounds,
                                 double seconds);
    // A node's shape around `center`, in the terrain's frame.
    void add(PhysicsWorld& physics, const ChunkKey& key, const v3d& center,
             const JPH::ShapeRefC& shape);
    void clear(PhysicsWorld& physics);

    bool wants(const ChunkKey& key) const { return wanted_.contains(key); }
    bool has(const ChunkKey& key) const { return bodies_.contains(key); }
    size_t size() const { return bodies_.size(); }
    int32_t level() const { return level_; }

   private:
    struct Body
    {
        JPH::BodyID id_;
        v3d center_ = v3d::Zero();
        double wantedAt_ = 0.0;
    };

    JPH::RVec3 positionOf(const v3d& center) const;
    void move(PhysicsWorld& physics, const Body& body) const;

    std::unordered_map<ChunkKey, Body, ChunkKeyHash> bodies_;
    ChunkKeySet wanted_;
    int32_t level_ = -1;
    double seconds_ = 0.0;
    v3d origin_ = v3d::Zero();
    quatd rotation_ = quatd::Identity();
    v3f velocity_ = v3f::Zero();
    Surface surface_;
};

}  // namespace batap
