#include "Components/EntityHandle.h"
#include "Instance/EntityFactory.h"
#include "Instance/Spawnable.h"
#include "Systems/Hierarchy_S.h"
#include "Systems/Systems.h"
#include "Systems/Transform_S.h"
#include "World.h"

#include "Physics/PhysicsWorld.h"

#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyLock.h>

#include "Components/Character_C.h"
#include "Components/RigidBody_C.h"
#include "Components/Transform_C.h"
#include "Physics/JoltConvert.h"

namespace batap
{
namespace
{
struct BodyRef
{
    JPH::BodyInterface* bodies_ = nullptr;
    JPH::BodyID id_;

    explicit operator bool() const { return bodies_ != nullptr; }
};
}  // namespace

static World& worldOf(const EntityHandle& h)
{
    ThrowAssert(h.reg_, "entityHandle has no registry");
    World* const* world = h.reg_->ctx().find<World*>();
    ThrowAssert(world, "registry has no World in its context");
    return **world;
}

static Transform_S& transformsOf(const EntityHandle& h)
{
    return *worldOf(h).systems().transforms_;
}

static BodyRef bodyOf(const EntityHandle& h)
{
    const RigidBody_C* rb = h.try_get<RigidBody_C>();
    if (!rb || rb->bodyId_ == kInvalidBodyId)
        return {};
    return {&worldOf(h).physics().bodies(), JPH::BodyID{rb->bodyId_}};
}

static void syncPhysicsPose(const EntityHandle& h)
{
    const Transform_C* tc = h.try_get<Transform_C>();
    if (!tc)
        return;

    if (h.try_get<Character_C>())
    {
        PhysicsWorld& physics = worldOf(h).physics();
        JPH::CharacterVirtual* cv = physics.character(entt::to_integral(h.entity_));
        if (!cv)
            return;

        cv->SetPosition(toJolt(tc->pos()));
        JPH::PhysicsSystem& system = physics.system();
        cv->RefreshContacts(system.GetDefaultBroadPhaseLayerFilter(objectLayers::Moving),
                            system.GetDefaultLayerFilter(objectLayers::Moving), {}, {},
                            physics.tempAllocator());
        return;
    }

    // Only a dynamic body owns its own pose. Physics_S already drives static
    // and kinematic ones from the transform, and a kinematic one must go
    // through MoveKinematic there or it teleports and carries nothing.
    RigidBody_C* rb = h.reg_->try_get<RigidBody_C>(h.entity_);
    if (!rb || rb->motion_ != RigidBody_C::Motion::Dynamic)
        return;

    const BodyRef b = bodyOf(h);
    if (!b)
        return;

    b.bodies_->SetPositionAndRotation(b.id_, toJolt(tc->pos()), toJolt(tc->rot()),
                                      JPH::EActivation::Activate);
    // Or the next frames would interpolate from where the body was.
    rb->prevPos_ = tc->pos();
    rb->prevRot_ = tc->rot();
}

void EntityHandle::markDirty(ComponentMask changed)
{
    worldOf(*this).instances().markDirty(*this, changed);
}

void EntityHandle::setLocalPosition(const v3f& p)
{
    transformsOf(*this).setLocalPosition(*this, p);
    syncPhysicsPose(*this);
}

void EntityHandle::setLocalRotation(const quatf& q)
{
    transformsOf(*this).setLocalRotation(*this, q);
    syncPhysicsPose(*this);
}

void EntityHandle::setLocalScale(const v3f& s)
{
    transformsOf(*this).setLocalScale(*this, s);
}

void EntityHandle::setPosition(const v3f& p, Space space)
{
    transformsOf(*this).setPosition(*this, p, space);
    syncPhysicsPose(*this);
}

void EntityHandle::setRotation(const quatf& q, Space space)
{
    transformsOf(*this).setRotation(*this, q, space);
    syncPhysicsPose(*this);
}

void EntityHandle::translate(const v3f& vec, Space space)
{
    transformsOf(*this).translate(*this, vec, space);
    syncPhysicsPose(*this);
}

void EntityHandle::rotate(const quatf& delta, Space space)
{
    transformsOf(*this).rotate(*this, delta, space);
    syncPhysicsPose(*this);
}

void EntityHandle::rotate(const v3f& axis, float radians, Space space)
{
    transformsOf(*this).rotate(*this, axis, radians, space);
    syncPhysicsPose(*this);
}

void EntityHandle::scale(const v3f& vec)
{
    transformsOf(*this).scale(*this, vec);
}

void EntityHandle::setParent(EntityHandle newParent)
{
    transformsOf(*this).setParent(*this, newParent);
}

EntityHandle EntityHandle::parent() const
{
    return {reg_, Hierarchy_S::getParent(*this)};
}

void EntityHandle::addForce(const v3f& force)
{
    if (const BodyRef b = bodyOf(*this))
        b.bodies_->AddForce(b.id_, toJolt(force));
}

void EntityHandle::addForce(const v3f& force, const v3f& worldPoint)
{
    if (const BodyRef b = bodyOf(*this))
        b.bodies_->AddForce(b.id_, toJolt(force), toJolt(worldPoint));
}

void EntityHandle::addTorque(const v3f& torque)
{
    if (const BodyRef b = bodyOf(*this))
        b.bodies_->AddTorque(b.id_, toJolt(torque));
}

void EntityHandle::addImpulse(const v3f& impulse)
{
    if (const BodyRef b = bodyOf(*this))
        b.bodies_->AddImpulse(b.id_, toJolt(impulse));
}

void EntityHandle::addImpulse(const v3f& impulse, const v3f& worldPoint)
{
    if (const BodyRef b = bodyOf(*this))
        b.bodies_->AddImpulse(b.id_, toJolt(impulse), toJolt(worldPoint));
}

void EntityHandle::addAngularImpulse(const v3f& angularImpulse)
{
    if (const BodyRef b = bodyOf(*this))
        b.bodies_->AddAngularImpulse(b.id_, toJolt(angularImpulse));
}

// Goes straight to the body: patching RigidBody_C would raise dirty_ and have
// Physics_S rebuild the shape, which a burning rocket would pay every tick.
void EntityHandle::setMass(float mass)
{
    RigidBody_C* rb = try_get<RigidBody_C>();
    if (!rb)
        return;
    rb->mass_ = mass;

    const BodyRef b = bodyOf(*this);
    if (!b)
        return;

    JPH::BodyLockWrite lock(worldOf(*this).physics().system().GetBodyLockInterface(), b.id_);
    if (!lock.Succeeded())
        return;

    JPH::MotionProperties* mp = lock.GetBody().GetMotionPropertiesUnchecked();
    if (!mp)
        return;

    JPH::MassProperties props = lock.GetBody().GetShape()->GetMassProperties();
    props.ScaleToMass(mass);
    mp->SetMassProperties(mp->GetAllowedDOFs(), props);
}

v3f EntityHandle::velocity() const
{
    const BodyRef b = bodyOf(*this);
    return b ? toEigen(b.bodies_->GetLinearVelocity(b.id_)) : v3f::Zero();
}

void EntityHandle::setVelocity(const v3f& v)
{
    if (const BodyRef b = bodyOf(*this))
        b.bodies_->SetLinearVelocity(b.id_, toJolt(v));
}

v3f EntityHandle::angularVelocity() const
{
    const BodyRef b = bodyOf(*this);
    return b ? toEigen(b.bodies_->GetAngularVelocity(b.id_)) : v3f::Zero();
}

void EntityHandle::setAngularVelocity(const v3f& v)
{
    if (const BodyRef b = bodyOf(*this))
        b.bodies_->SetAngularVelocity(b.id_, toJolt(v));
}

EntityHandle World::spawn(std::string_view spawnableId)
{
    return spawn(spawnableFor(spawnableId));
}

EntityHandle World::spawn(const Spawnable& spawnable)
{
    return entityFactory_->create(registry_, spawnable);
}

void World::destroy(EntityHandle h)
{
    entityFactory_->destroy(h);
}
}  // namespace batap
