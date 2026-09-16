#include "Physics/ContactCollector.h"

#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Collision/Shape/SubShapeIDPair.h>

#include "Physics/JoltConvert.h"

namespace batap
{

void ContactCollector::OnContactAdded(const JPH::Body& body1, const JPH::Body& body2,
                                      const JPH::ContactManifold& manifold, JPH::ContactSettings&)
{
    RawContact raw;
    raw.bodyA_ = body1.GetID().GetIndexAndSequenceNumber();
    raw.bodyB_ = body2.GetID().GetIndexAndSequenceNumber();
    raw.entered_ = true;
    raw.normal_ = toEigen(manifold.mWorldSpaceNormal);

    if (!manifold.mRelativeContactPointsOn1.empty())
    {
        const JPH::Vec3 point = manifold.GetWorldSpaceContactPointOn1(0);
        raw.point_ = toEigen(point);
        // The solver has not run yet, so these are the velocities before the
        // collision response wiped them — the only chance to read an impact.
        raw.closingSpeed_ = (body1.GetPointVelocity(point) - body2.GetPointVelocity(point))
                                .Dot(manifold.mWorldSpaceNormal);
    }

    const std::lock_guard<std::mutex> guard(mutex_);
    pending_.push_back(raw);
}

// A body falling asleep drops all its contacts, so resting on a sensor emits a
// spurious exit — and a matching enter when it wakes again.
void ContactCollector::OnContactRemoved(const JPH::SubShapeIDPair& pair)
{
    RawContact raw;
    raw.bodyA_ = pair.GetBody1ID().GetIndexAndSequenceNumber();
    raw.bodyB_ = pair.GetBody2ID().GetIndexAndSequenceNumber();

    const std::lock_guard<std::mutex> guard(mutex_);
    pending_.push_back(raw);
}

void ContactCollector::take(std::vector<RawContact>& out)
{
    const std::lock_guard<std::mutex> guard(mutex_);
    out.swap(pending_);
    pending_.clear();
}

void ContactCollector::clear()
{
    const std::lock_guard<std::mutex> guard(mutex_);
    pending_.clear();
}

}  // namespace batap
