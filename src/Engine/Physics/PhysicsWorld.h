#pragma once

#include <Jolt/Jolt.h>

#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Constraints/Constraint.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include "Assets/AssetHandle.h"
#include "Physics/ContactCollector.h"
#include "Physics/PhysicsLayers.h"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace batap
{

struct AssetManager;

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
    float lastStepMs() const { return lastStepMs_; }

    JPH::PhysicsSystem& system() { return system_; }
    JPH::BodyInterface& bodies() { return system_.GetBodyInterface(); }
    ContactCollector& contacts() { return contacts_; }
    JPH::TempAllocator& tempAllocator() { return tempAllocator_; }

    void setMaxBodySpeed(float speed);
    float maxBodySpeed() const { return maxBodySpeed_; }

    JPH::CharacterVirtual* character(uint32_t key);
    JPH::CharacterVirtual& createCharacter(uint32_t key, const JPH::CharacterVirtualSettings& s,
                                           JPH::RVec3Arg pos, JPH::QuatArg rot);
    void destroyCharacter(uint32_t key);

    void shift(const v3f& by);

    // Constraints are keyed and owned here for one reason: they must leave the
    // PhysicsSystem before the bodies they hold, and clear() is where that is
    // known. listener is for the constraints that are also step listeners.
    // Kept until clear(): a re-imported mesh shows after the scene reloads.
    JPH::ShapeRefC meshShape(AssetManager& assets, MeshHandle mesh, bool hull);

    JPH::Constraint* constraint(uint32_t key);
    JPH::Constraint& addConstraint(uint32_t key, JPH::Constraint* constraint,
                                   JPH::PhysicsStepListener* listener = nullptr);
    void destroyConstraint(uint32_t key);

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
    float maxBodySpeed_ = JPH::BodyCreationSettings{}.mMaxLinearVelocity;
    float lastStepMs_ = 0.f;
    uint32_t reportedErrors_ = 0;

    struct OwnedConstraint
    {
        JPH::Ref<JPH::Constraint> constraint_;
        JPH::PhysicsStepListener* listener_ = nullptr;
    };

    // After system_, so they are destroyed before it: a CharacterVirtual holds
    // a pointer to the PhysicsSystem it queries, and a constraint is
    // registered inside it.
    std::unordered_map<uint32_t, JPH::Ref<JPH::CharacterVirtual>> characters_;
    std::unordered_map<uint32_t, OwnedConstraint> constraints_;
    std::unordered_map<std::string, JPH::ShapeRefC> hullShapes_;
    std::unordered_map<std::string, JPH::ShapeRefC> triangleShapes_;
};

}  // namespace batap
