#pragma once

#include <Jolt/Jolt.h>

#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include "Physics/ContactCollector.h"
#include "Physics/PhysicsLayers.h"

#include <cstdint>
#include <unordered_map>

namespace batap
{

struct JoltRuntime
{
    JoltRuntime();
    ~JoltRuntime();

    JoltRuntime(const JoltRuntime&) = delete;
    JoltRuntime& operator=(const JoltRuntime&) = delete;
};

struct PhysicsWorld
{
    PhysicsWorld();
    ~PhysicsWorld();

    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    void clear();
    void step(float dt);

    JPH::PhysicsSystem& system() { return system_; }
    JPH::BodyInterface& bodies() { return system_.GetBodyInterface(); }
    ContactCollector& contacts() { return contacts_; }
    JPH::TempAllocator& tempAllocator() { return tempAllocator_; }

    JPH::CharacterVirtual* character(uint32_t key);
    JPH::CharacterVirtual& createCharacter(uint32_t key, const JPH::CharacterVirtualSettings& s,
                                           JPH::RVec3Arg pos, JPH::QuatArg rot);
    void destroyCharacter(uint32_t key);

   private:
    // Declared first: TempAllocatorImpl allocates through Jolt's allocator,
    // which only exists once JoltRuntime has registered it.
    JoltRuntime runtime_;

    JPH::TempAllocatorImpl tempAllocator_;
    JPH::JobSystemThreadPool jobs_;

    // Init() keeps references to the first three and SetContactListener to the
    // fourth, so they must outlive system_: declared before it, hence
    // destroyed after it.
    BroadPhaseLayerMap bpLayers_;
    ObjectVsBroadPhaseFilter objVsBp_;
    ObjectPairFilter objPair_;
    ContactCollector contacts_;

    JPH::PhysicsSystem system_;

    // After system_, so they are destroyed before it: a CharacterVirtual holds
    // a pointer to the PhysicsSystem it queries.
    std::unordered_map<uint32_t, JPH::Ref<JPH::CharacterVirtual>> characters_;
};

}  // namespace batap
