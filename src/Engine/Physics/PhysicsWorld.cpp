#include "Physics/PhysicsWorld.h"

#include <Jolt/Core/Factory.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
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
    system_.Update(dt, kCollisionSteps, &tempAllocator_, &jobs_);
}

}  // namespace batap
