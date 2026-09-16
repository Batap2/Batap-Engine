#pragma once

#include <Jolt/Jolt.h>

#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Collision/ContactListener.h>

#include "EigenTypes.h"

#include <cstdint>
#include <mutex>
#include <vector>

namespace batap
{

struct RawContact
{
    uint32_t bodyA_ = JPH::BodyID::cInvalidBodyID;
    uint32_t bodyB_ = JPH::BodyID::cInvalidBodyID;
    bool entered_ = false;

    v3f point_ = v3f::Zero();
    v3f normal_ = v3f::Zero();
    float closingSpeed_ = 0.f;
};

// Jolt calls these from its worker threads with every body locked: they may
// only read from the bodies, and nothing here may touch the registry.
struct ContactCollector final : JPH::ContactListener
{
    void OnContactAdded(const JPH::Body& body1, const JPH::Body& body2,
                        const JPH::ContactManifold& manifold,
                        JPH::ContactSettings& settings) override;
    void OnContactRemoved(const JPH::SubShapeIDPair& pair) override;

    void take(std::vector<RawContact>& out);
    void clear();

   private:
    std::mutex mutex_;
    std::vector<RawContact> pending_;
};

}  // namespace batap
