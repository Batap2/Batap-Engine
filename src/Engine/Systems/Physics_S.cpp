#include "Systems/Physics_S.h"

#include "Physics/PhysicsWorld.h"

#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>

#include "Components/RigidBody_C.h"
#include "Components/Transform_C.h"
#include "Physics/JoltConvert.h"
#include "Renderer/DebugDraw.h"
#include "Systems/Systems.h"
#include "Systems/Transform_S.h"
#include "World.h"

#include <algorithm>
#include <numbers>

namespace batap
{
namespace
{

static_assert(kInvalidBodyId == JPH::BodyID::cInvalidBodyID);

JPH::EMotionType motionTypeOf(RigidBody_C::Motion m)
{
    switch (m)
    {
        case RigidBody_C::Motion::Static:
            return JPH::EMotionType::Static;
        case RigidBody_C::Motion::Kinematic:
            return JPH::EMotionType::Kinematic;
        case RigidBody_C::Motion::Dynamic:
            break;
    }
    return JPH::EMotionType::Dynamic;
}

JPH::ObjectLayer layerOf(RigidBody_C::Motion m)
{
    return m == RigidBody_C::Motion::Static ? objectLayers::NonMoving : objectLayers::Moving;
}

JPH::EActivation activationOf(RigidBody_C::Motion m)
{
    return m == RigidBody_C::Motion::Dynamic ? JPH::EActivation::Activate
                                             : JPH::EActivation::DontActivate;
}

quatf localRotOf(const Shape& s)
{
    constexpr float kDegToRad = std::numbers::pi_v<float> / 180.f;
    const v3f r = s.localRotDeg_ * kDegToRad;
    return (angleaxisf(r.x(), v3f::UnitX()) * angleaxisf(r.y(), v3f::UnitY()) *
            angleaxisf(r.z(), v3f::UnitZ()))
        .normalized();
}

bool isCentered(const Shape& s)
{
    return s.localPos_.isZero() && s.localRotDeg_.isZero();
}

JPH::ShapeRefC primitiveOf(const Shape& s)
{
    switch (s.kind_)
    {
        case Shape::Kind::Sphere:
            return new JPH::SphereShape(s.radius_);
        case Shape::Kind::Capsule:
            return new JPH::CapsuleShape(s.halfHeight_, s.radius_);
        case Shape::Kind::Box:
            break;
    }
    // Jolt asserts if the rounding radius eats the box; a thin collider is
    // legitimate here, the editor lets the extents go down to 1 mm.
    const float smallest = s.halfExtents_.minCoeff();
    return new JPH::BoxShape(toJolt(s.halfExtents_),
                             std::min(JPH::cDefaultConvexRadius, smallest * 0.5f));
}

JPH::ShapeRefC makeUnscaledShape(const RigidBody_C& rb)
{
    if (rb.shapes_.size() == 1)
    {
        const Shape& s = rb.shapes_.front();
        if (isCentered(s))
            return primitiveOf(s);
        // A StaticCompoundShape needs at least two children, so a lone
        // offset shape is wrapped instead.
        return new JPH::RotatedTranslatedShape(toJolt(s.localPos_), toJolt(localRotOf(s)),
                                               primitiveOf(s));
    }

    JPH::StaticCompoundShapeSettings settings;
    for (const Shape& s : rb.shapes_)
        settings.AddShape(toJolt(s.localPos_), toJolt(localRotOf(s)), primitiveOf(s));

    return settings.Create().Get();
}

JPH::ShapeRefC makeShape(const RigidBody_C& rb, const v3f& scale)
{
    JPH::ShapeRefC base = makeUnscaledShape(rb);
    const JPH::Vec3 s = toJolt(scale);
    if (s.IsClose(JPH::Vec3::sOne()))
        return base;

    // A sphere only accepts a uniform scale and a capsule a uniform X/Z one,
    // and a compound rejects any non-uniform scale as soon as one of its
    // children is rotated: MakeScaleValid picks the closest legal scale (and
    // a non-zero one) instead of letting Jolt assert on whatever the
    // inspector produced.
    return new JPH::ScaledShape(base, base->MakeScaleValid(s));
}

const col3& colorOf(const RigidBody_C& rb)
{
    if (!rb.active_)
        return colors::grey;
    switch (rb.motion_)
    {
        case RigidBody_C::Motion::Static:
            return colors::green;
        case RigidBody_C::Motion::Kinematic:
            return colors::blue;
        case RigidBody_C::Motion::Dynamic:
            break;
    }
    return colors::cyan;
}

// Damping and mass are not on BodyInterface, and SetShape would recompute the
// mass from the shape's density and drop mass_.
void applyMotionProperties(PhysicsWorld& physics, JPH::BodyID id, const RigidBody_C& rb,
                           const JPH::Shape& shape)
{
    JPH::BodyLockWrite lock(physics.system().GetBodyLockInterface(), id);
    if (!lock.Succeeded())
        return;

    JPH::MotionProperties* mp = lock.GetBody().GetMotionPropertiesUnchecked();
    if (!mp)
        return;

    mp->SetLinearDamping(rb.linearDamping_);
    mp->SetAngularDamping(rb.angularDamping_);

    JPH::MassProperties mass = shape.GetMassProperties();
    mass.ScaleToMass(rb.mass_);
    mp->SetMassProperties(mp->GetAllowedDOFs(), mass);
}

void destroyBody(JPH::BodyInterface& bi, RigidBody_C& rb)
{
    if (rb.bodyId_ == kInvalidBodyId)
        return;

    const JPH::BodyID id{rb.bodyId_};
    bi.RemoveBody(id);
    bi.DestroyBody(id);
    rb.bodyId_ = kInvalidBodyId;
}

void createBody(JPH::BodyInterface& bi, RigidBody_C& rb, const Transform_C& tc)
{
    rb.shapeScale_ = tc.scale();
    rb.dirty_ = false;
    JPH::BodyCreationSettings settings(makeShape(rb, rb.shapeScale_), toJolt(tc.pos()),
                                       toJolt(tc.rot()), motionTypeOf(rb.motion_),
                                       layerOf(rb.motion_));
    settings.mFriction = rb.friction_;
    settings.mRestitution = rb.restitution_;
    settings.mLinearDamping = rb.linearDamping_;
    settings.mAngularDamping = rb.angularDamping_;
    settings.mGravityFactor = rb.gravityFactor_;
    settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
    settings.mMassPropertiesOverride.mMass = rb.mass_;

    rb.bodyId_ =
        bi.CreateAndAddBody(settings, activationOf(rb.motion_)).GetIndexAndSequenceNumber();
}

}  // namespace

void Physics_S::connectHooks(entt::registry& reg)
{
    reg.on_destroy<RigidBody_C>().connect<&Physics_S::onRigidBodyDestroyed>(*this);
    reg.on_update<RigidBody_C>().connect<&Physics_S::onRigidBodyChanged>(*this);
}

void Physics_S::onRigidBodyChanged(entt::registry& reg, entt::entity e)
{
    reg.get<RigidBody_C>(e).dirty_ = true;
}

void Physics_S::onRigidBodyDestroyed(entt::registry& reg, entt::entity e)
{
    World** world = reg.ctx().find<World*>();
    if (!world)
        return;

    destroyBody((*world)->physics().bodies(), reg.get<RigidBody_C>(e));
}

void Physics_S::drawColliders(World& world)
{
    if (!showColliders_)
        return;

    DebugDraw& dbg = world.debugOverlay();
    for (auto [e, rb, tc] : world.registry_.view<RigidBody_C, Transform_C>().each())
    {
        const transform base = TRS_Transform(tc.pos(), tc.rot(), v3f::Ones());
        const v3f scale = tc.scale().cwiseAbs();
        const col3& color = colorOf(rb);

        for (const Shape& s : rb.shapes_)
        {
            // The entity scale moves a child's offset as well as its size —
            // that is what ScaledShape does to the compound.
            const transform xform =
                base * TRS_Transform(s.localPos_.cwiseProduct(scale), localRotOf(s), v3f::Ones());

            switch (s.kind_)
            {
                case Shape::Kind::Box:
                    dbg.box(xform, s.halfExtents_.cwiseProduct(scale), color);
                    break;
                case Shape::Kind::Sphere:
                    // Jolt only accepts a uniform scale on a sphere and a uniform
                    // X/Z one on a capsule (MakeScaleValid); the wire reproduces that
                    dbg.sphere(xform, s.radius_ * scale.sum() / 3.f, color);
                    break;
                case Shape::Kind::Capsule:
                    dbg.capsule(xform, s.halfHeight_ * scale.y(),
                                s.radius_ * (scale.x() + scale.z()) * 0.5f, color);
                    break;
            }
        }
    }
}

void Physics_S::fixedUpdate(World& world, float dt)
{
    auto& reg = world.registry_;
    PhysicsWorld& physics = world.physics();
    JPH::BodyInterface& bi = physics.bodies();

    auto view = reg.view<RigidBody_C, Transform_C>();

    for (auto e : view)
    {
        auto& rb = view.get<RigidBody_C>(e);

        if (!rb.active_ || rb.shapes_.empty())
        {
            destroyBody(bi, rb);
            continue;
        }

        const auto& tc = view.get<Transform_C>(e);
        if (rb.bodyId_ == kInvalidBodyId)
        {
            createBody(bi, rb, tc);
            continue;
        }

        const JPH::BodyID id{rb.bodyId_};

        if (rb.dirty_ || !tc.scale().isApprox(rb.shapeScale_))
        {
            rb.dirty_ = false;
            rb.shapeScale_ = tc.scale();

            JPH::ShapeRefC shape = makeShape(rb, rb.shapeScale_);
            bi.SetShape(id, shape, false, activationOf(rb.motion_));
            bi.SetFriction(id, rb.friction_);
            bi.SetRestitution(id, rb.restitution_);
            bi.SetGravityFactor(id, rb.gravityFactor_);
            applyMotionProperties(physics, id, rb, *shape);
        }

        const JPH::EMotionType wanted = motionTypeOf(rb.motion_);
        const JPH::EMotionType current = bi.GetMotionType(id);
        if (current != wanted)
        {
            // Jolt only gives a body MotionProperties when it is created
            // non-static, so it cannot be switched into or out of Static in
            // place — that crossing costs a new body.
            if (current == JPH::EMotionType::Static || wanted == JPH::EMotionType::Static)
            {
                destroyBody(bi, rb);
                createBody(bi, rb, tc);
                continue;
            }
            bi.SetObjectLayer(id, layerOf(rb.motion_));
            bi.SetMotionType(id, wanted, activationOf(rb.motion_));
        }

        if (rb.motion_ == RigidBody_C::Motion::Kinematic)
            bi.MoveKinematic(id, toJolt(tc.pos()), toJolt(tc.rot()), dt);
        else if (rb.motion_ == RigidBody_C::Motion::Static)
            bi.SetPositionAndRotationWhenChanged(id, toJolt(tc.pos()), toJolt(tc.rot()),
                                                 JPH::EActivation::DontActivate);
    }

    physics.step(dt);

    Transform_S& transforms = *world.systems().transforms_;
    for (auto e : view)
    {
        const auto& rb = view.get<RigidBody_C>(e);
        if (rb.motion_ != RigidBody_C::Motion::Dynamic || rb.bodyId_ == kInvalidBodyId)
            continue;

        const JPH::BodyID id{rb.bodyId_};
        if (!bi.IsActive(id))  // asleep: its pose did not move
            continue;

        const EntityHandle h{&reg, e};
        transforms.setLocalPosition(h, toEigen(bi.GetPosition(id)));
        transforms.setLocalRotation(h, toEigen(bi.GetRotation(id)));
    }
}

}  // namespace batap
