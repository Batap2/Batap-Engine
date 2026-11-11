#include "Physics/PhysicsWorld.h"

#include "Assets/AssetManager.h"
#include "Assets/Mesh.h"
#include "Physics/JoltConvert.h"
#include "Physics/MeshCollider.h"
#include "Physics/PhysicsLayers.h"
#include "Serialization/BmeshSerializer.h"

#include <Jolt/Core/Factory.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>

namespace batap
{
namespace
{

constexpr JPH::uint kTempAllocatorBytes = 16u * 1024u * 1024u;
constexpr JPH::uint kMaxBodies = 16384u;
constexpr JPH::uint kNumBodyMutexes = 0u;
constexpr JPH::uint kMaxBodyPairs = 16384u;
constexpr JPH::uint kMaxContactConstraints = 8192u;
constexpr int kCollisionSteps = 1;

// Host-side only: the game DLL links its own copy of the engine lib and would
// get a second counter.
int liveRuntimes = 0;

int workerThreadCount()
{
    const int cores = static_cast<int>(std::thread::hardware_concurrency());
    return std::max(1, cores - 1);
}

}  // namespace

JoltRuntime::JoltRuntime()
{
    if (liveRuntimes++ > 0)
        return;

    JPH::RegisterDefaultAllocator();
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();
}

JoltRuntime::~JoltRuntime()
{
    if (--liveRuntimes > 0)
        return;

    JPH::UnregisterTypes();
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;
}

PhysicsWorld::PhysicsWorld()
    : tempAllocator_(kTempAllocatorBytes),
      jobs_(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, workerThreadCount())
{
    system_.Init(kMaxBodies, kNumBodyMutexes, kMaxBodyPairs, kMaxContactConstraints, bpLayers_,
                 objVsBp_, objPair_);
    system_.SetContactListener(&contacts_);
}

PhysicsWorld::~PhysicsWorld()
{
    clear();
}

JPH::CharacterVirtual* PhysicsWorld::character(uint32_t key)
{
    const auto it = characters_.find(key);
    return it == characters_.end() ? nullptr : it->second.GetPtr();
}

JPH::CharacterVirtual& PhysicsWorld::createCharacter(uint32_t key,
                                                     const JPH::CharacterVirtualSettings& s,
                                                     JPH::RVec3Arg pos, JPH::QuatArg rot)
{
    JPH::Ref<JPH::CharacterVirtual> character = new JPH::CharacterVirtual(&s, pos, rot, &system_);
    characters_[key] = character;
    return *character;
}

void PhysicsWorld::destroyCharacter(uint32_t key)
{
    characters_.erase(key);
}

void PhysicsWorld::shift(const v3f& by)
{
    JPH::BodyInterface& bi = bodies();
    JPH::BodyIDVector ids;
    system_.GetBodies(ids);
    for (const JPH::BodyID id : ids)
        bi.SetPosition(id, bi.GetPosition(id) + toJolt(by), JPH::EActivation::DontActivate);

    for (auto& [key, character] : characters_)
    {
        character->SetPosition(character->GetPosition() + toJolt(by));
        character->RefreshContacts(system_.GetDefaultBroadPhaseLayerFilter(objectLayers::Moving),
                                   system_.GetDefaultLayerFilter(objectLayers::Moving), {}, {},
                                   tempAllocator_);
    }
}

JPH::Constraint* PhysicsWorld::constraint(uint32_t key)
{
    const auto it = constraints_.find(key);
    return it == constraints_.end() ? nullptr : it->second.constraint_.GetPtr();
}

JPH::Constraint& PhysicsWorld::addConstraint(uint32_t key, JPH::Constraint* constraint,
                                             JPH::PhysicsStepListener* listener)
{
    destroyConstraint(key);

    system_.AddConstraint(constraint);
    if (listener)
        system_.AddStepListener(listener);

    constraints_[key] = OwnedConstraint{constraint, listener};
    return *constraint;
}

void PhysicsWorld::destroyConstraint(uint32_t key)
{
    const auto it = constraints_.find(key);
    if (it == constraints_.end())
        return;

    if (it->second.listener_)
        system_.RemoveStepListener(it->second.listener_);
    system_.RemoveConstraint(it->second.constraint_);
    constraints_.erase(it);
}

JPH::ShapeRefC PhysicsWorld::meshShape(AssetManager& assets, MeshHandle mesh, bool hull)
{
    const std::string* path = mesh ? assets.getPath(mesh) : nullptr;
    if (!path)
        return nullptr;
    JPH::ShapeRefC& shared = (hull ? hullShapes_ : triangleShapes_)[*path];
    if (!shared)
        if (const auto data = readBmesh((std::filesystem::path(assets.baseDir()) / *path).string()))
            shared = hull ? makeHullShape(*data) : makeMeshShape(*data);
    return shared;
}

void PhysicsWorld::setMaxBodySpeed(float speed)
{
    maxBodySpeed_ = speed;
    JPH::BodyIDVector ids;
    system_.GetBodies(ids);
    JPH::BodyInterface& bi = bodies();
    for (const JPH::BodyID& id : ids)
        if (bi.GetMotionType(id) == JPH::EMotionType::Dynamic)
            bi.SetMaxLinearVelocity(id, speed);
}

void PhysicsWorld::clear()
{
    contacts_.clear();
    characters_.clear();

    // Before the bodies below: a constraint left in the system would keep a
    // pointer to one that is about to be destroyed.
    for (auto& [key, owned] : constraints_)
    {
        if (owned.listener_)
            system_.RemoveStepListener(owned.listener_);
        system_.RemoveConstraint(owned.constraint_);
    }
    constraints_.clear();
    hullShapes_.clear();
    triangleShapes_.clear();

    JPH::BodyIDVector ids;
    system_.GetBodies(ids);
    if (ids.empty())
        return;

    // RemoveBodies asserts on a body that was never added: every body must be
    // created via CreateAndAddBody, never left in the created-only state.
    JPH::BodyInterface& bi = system_.GetBodyInterface();
    const int count = static_cast<int>(ids.size());
    bi.RemoveBodies(ids.data(), count);
    bi.DestroyBodies(ids.data(), count);
}

void PhysicsWorld::step(float dt)
{
    const auto start = std::chrono::steady_clock::now();
    const auto errors =
        static_cast<uint32_t>(system_.Update(dt, kCollisionSteps, &tempAllocator_, &jobs_));
    lastStepMs_ =
        std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();

    const uint32_t fresh = errors & ~reportedErrors_;
    reportedErrors_ |= errors;
    if (fresh & static_cast<uint32_t>(JPH::EPhysicsUpdateError::ManifoldCacheFull))
        std::cerr << "[Physics] manifold cache full: contacts are being dropped\n";
    if (fresh & static_cast<uint32_t>(JPH::EPhysicsUpdateError::BodyPairCacheFull))
        std::cerr << "[Physics] body pair cache full (kMaxBodyPairs): contacts are being dropped\n";
    if (fresh & static_cast<uint32_t>(JPH::EPhysicsUpdateError::ContactConstraintsFull))
        std::cerr << "[Physics] contact constraints full (kMaxContactConstraints): contacts are "
                     "being dropped\n";
}

}  // namespace batap
