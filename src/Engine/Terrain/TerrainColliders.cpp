#include "Terrain/TerrainColliders.h"

#include "Physics/JoltConvert.h"
#include "Physics/PhysicsLayers.h"
#include "Physics/PhysicsWorld.h"

#include <Jolt/Geometry/AABox.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/MassProperties.h>

#include <bit>
#include <limits>

namespace batap
{
namespace
{

// A body that turns back finds the ground it just left.
constexpr double kLingerSeconds = 2.0;

bool sameSurface(const TerrainColliders::Surface& a, const TerrainColliders::Surface& b)
{
    return a.userData_ == b.userData_ &&
           std::bit_cast<uint32_t>(a.friction_) == std::bit_cast<uint32_t>(b.friction_) &&
           std::bit_cast<uint32_t>(a.restitution_) == std::bit_cast<uint32_t>(b.restitution_);
}

}  // namespace

JPH::RVec3 TerrainColliders::positionOf(const v3d& center) const
{
    return toJolt(v3f{(origin_ + rotation_ * center).cast<float>()});
}

void TerrainColliders::move(PhysicsWorld& physics, const Body& body) const
{
    JPH::BodyInterface& bi = physics.bodies();
    const JPH::Quat rot = toJolt(quatf{rotation_.cast<float>()});
    const JPH::EMotionType motion = velocity_.isZero() ? JPH::EMotionType::Static
                                                       : JPH::EMotionType::Kinematic;
    if (bi.GetMotionType(body.id_) != motion)
        bi.SetMotionType(body.id_, motion, JPH::EActivation::DontActivate);
    if (motion == JPH::EMotionType::Static)
        bi.SetPositionAndRotation(body.id_, positionOf(body.center_), rot,
                                  JPH::EActivation::DontActivate);
    else
        bi.SetPositionRotationAndVelocity(body.id_, positionOf(body.center_), rot,
                                          toJolt(velocity_), JPH::Vec3::sZero());
}

void TerrainColliders::place(PhysicsWorld& physics, const v3d& origin, const quatd& rotation,
                             const v3f& velocity, const Surface& surface)
{
    JPH::BodyInterface& bi = physics.bodies();
    if (!sameSurface(surface, surface_))
    {
        surface_ = surface;
        for (const auto& [key, body] : bodies_)
        {
            bi.SetFriction(body.id_, surface_.friction_);
            bi.SetRestitution(body.id_, surface_.restitution_);
            bi.SetUserData(body.id_, surface_.userData_);
        }
    }
    if (origin == origin_ && rotation.coeffs() == rotation_.coeffs() && velocity == velocity_)
        return;
    origin_ = origin;
    rotation_ = rotation;
    velocity_ = velocity;
    for (const auto& [key, body] : bodies_)
        move(physics, body);
}

std::vector<ChunkKey> TerrainColliders::update(PhysicsWorld& physics, const TerrainGrid& grid,
                                               int32_t level, const std::vector<Reach>& reaches,
                                               const NodeBoundsFn& bounds, double seconds)
{
    if (level != level_)
    {
        clear(physics);
        level_ = level;
    }
    seconds_ = seconds;
    const std::vector<ChunkKey> within = nodesWithin(grid, level, reaches, bounds);
    wanted_ = ChunkKeySet(within.begin(), within.end());

    JPH::BodyInterface& bi = physics.bodies();
    for (auto it = bodies_.begin(); it != bodies_.end();)
    {
        Body& body = it->second;
        if (wanted_.contains(it->first))
            body.wantedAt_ = seconds;
        if (seconds - body.wantedAt_ <= kLingerSeconds)
        {
            ++it;
            continue;
        }
        bi.RemoveBody(body.id_);
        bi.DestroyBody(body.id_);
        it = bodies_.erase(it);
    }

    std::vector<ChunkKey> missing;
    for (const ChunkKey& key : within)
        if (!bodies_.contains(key))
            missing.push_back(key);
    return missing;
}

void TerrainColliders::add(PhysicsWorld& physics, const ChunkKey& key, const v3d& center,
                           const JPH::ShapeRefC& shape)
{
    if (!shape || bodies_.contains(key))
        return;
    JPH::BodyCreationSettings settings(shape, positionOf(center),
                                       toJolt(quatf{rotation_.cast<float>()}),
                                       JPH::EMotionType::Static, objectLayers::NonMoving);
    settings.mFriction = surface_.friction_;
    settings.mRestitution = surface_.restitution_;
    settings.mUserData = surface_.userData_;
    settings.mAllowDynamicOrKinematic = true;
    // A mesh has no mass properties of its own, and Jolt refuses the NaN.
    settings.mOverrideMassProperties = JPH::EOverrideMassProperties::MassAndInertiaProvided;
    settings.mMassPropertiesOverride.SetMassAndInertiaOfSolidBox(
        JPH::Vec3::sMax(shape->GetLocalBounds().GetSize(), JPH::Vec3::sReplicate(1e-3f)), 1.f);
    settings.mMaxLinearVelocity = std::numeric_limits<float>::max();
    const JPH::BodyID id =
        physics.bodies().CreateAndAddBody(settings, JPH::EActivation::DontActivate);
    if (id.IsInvalid())
        return;
    const Body& body = bodies_.emplace(key, Body{id, center, seconds_}).first->second;
    if (!velocity_.isZero())
        move(physics, body);
}

void TerrainColliders::clear(PhysicsWorld& physics)
{
    JPH::BodyInterface& bi = physics.bodies();
    for (const auto& [key, body] : bodies_)
    {
        bi.RemoveBody(body.id_);
        bi.DestroyBody(body.id_);
    }
    bodies_.clear();
    wanted_.clear();
    level_ = -1;
}

}  // namespace batap
