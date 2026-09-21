#include "Systems/Physics_S.h"

#include "Physics/PhysicsWorld.h"

#include <Jolt/Geometry/AABox.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Body/MassProperties.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/OffsetCenterOfMassShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>

#include "Assets/AssetManager.h"
#include "Assets/Mesh.h"
#include "Components/RigidBody_C.h"
#include "Components/Transform_C.h"
#include "Physics/JoltConvert.h"
#include "Physics/MeshCollider.h"
#include "Renderer/DebugDraw.h"
#include "Serialization/BmeshSerializer.h"
#include "Systems/Systems.h"
#include "Systems/Transform_S.h"
#include "World.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
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

// The triangles come from the file, not from the GPU copy: the loader keeps
// no CPU vertices. Read once per build, never per tick.
JPH::ShapeRefC meshShapeOf(const Shape& s, AssetManager& assets)
{
    const std::string* path = s.mesh_ ? assets.getPath(s.mesh_) : nullptr;
    if (path)
        if (const auto data = readBmesh((std::filesystem::path(assets.baseDir()) / *path).string()))
            if (JPH::ShapeRefC shape = makeMeshShape(*data))
                return shape;

    std::cerr << "[Physics_S] mesh collider unavailable" << (path ? " : " + *path : std::string{})
              << ", falling back to a unit sphere\n";
    return new JPH::SphereShape(1.f);
}

JPH::ShapeRefC primitiveOf(const Shape& s, AssetManager& assets)
{
    switch (s.kind_)
    {
        case Shape::Kind::Sphere:
            return new JPH::SphereShape(s.radius_);
        case Shape::Kind::Capsule:
            return new JPH::CapsuleShape(s.halfHeight_, s.radius_);
        case Shape::Kind::Mesh:
            return meshShapeOf(s, assets);
        case Shape::Kind::Box:
            break;
    }
    // Jolt asserts if the rounding radius eats the box; a thin collider is
    // legitimate here, the editor lets the extents go down to 1 mm.
    const float smallest = s.halfExtents_.minCoeff();
    return new JPH::BoxShape(toJolt(s.halfExtents_),
                             std::min(JPH::cDefaultConvexRadius, smallest * 0.5f));
}

JPH::ShapeRefC makeUnscaledShape(const RigidBody_C& rb, AssetManager& assets)
{
    if (rb.shapes_.size() == 1)
    {
        const Shape& s = rb.shapes_.front();
        if (isCentered(s))
            return primitiveOf(s, assets);
        // A StaticCompoundShape needs at least two children, so a lone
        // offset shape is wrapped instead.
        return new JPH::RotatedTranslatedShape(toJolt(s.localPos_), toJolt(localRotOf(s)),
                                               primitiveOf(s, assets));
    }

    JPH::StaticCompoundShapeSettings settings;
    for (const Shape& s : rb.shapes_)
        settings.AddShape(toJolt(s.localPos_), toJolt(localRotOf(s)), primitiveOf(s, assets));

    return settings.Create().Get();
}

JPH::ShapeRefC makeShape(const RigidBody_C& rb, const v3f& scale, AssetManager& assets)
{
    JPH::ShapeRefC base = makeUnscaledShape(rb, assets);
    if (!rb.centerOfMassOffset_.isZero())
        base = new JPH::OffsetCenterOfMassShape(base, toJolt(rb.centerOfMassOffset_));
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

// A mesh has no volume, so Jolt hands back invalid mass properties for it and
// ScaleToMass would divide by zero: a mesh (or anything wrapping one) gets the
// inertia of the solid box of its bounds instead. A kinematic body never reads
// them, but Jolt refuses the NaN in debug.
JPH::MassProperties massPropertiesOf(const JPH::Shape& shape, float mass)
{
    JPH::MassProperties mp;
    if (shape.MustBeStatic())
    {
        const JPH::AABox bounds = shape.GetLocalBounds();
        const JPH::Vec3 size = JPH::Vec3::sMax(bounds.GetSize(), JPH::Vec3::sReplicate(1e-3f));
        mp.SetMassAndInertiaOfSolidBox(size, 1.f);
    }
    else
        mp = shape.GetMassProperties();
    mp.ScaleToMass(mass);
    return mp;
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

// A mesh collider has no wire of its own, so its bounds stand in for it. The
// four body diagonals, dimmer than the wire, mark the box as a stand-in: a
// plain box would read as a box collider.
void drawMeshProxy(DebugDraw& dbg, const transform& xform, const v3f& half, const col3& color)
{
    dbg.box(xform, half, color);

    const col3 hatch{color * 0.55f};
    for (int corner = 0; corner < 4; ++corner)
    {
        const v3f a{half.x(), corner & 1 ? -half.y() : half.y(),
                    corner & 2 ? -half.z() : half.z()};
        const v3f b = -a;
        dbg.line(xform * a, xform * b, hatch);
    }
}

// Damping, mass and the sensor flag are not on BodyInterface, and SetShape
// would recompute the mass from the shape's density and drop mass_.
void applyLockedProperties(PhysicsWorld& physics, JPH::BodyID id, const RigidBody_C& rb,
                           const JPH::Shape& shape)
{
    JPH::BodyLockWrite lock(physics.system().GetBodyLockInterface(), id);
    if (!lock.Succeeded())
        return;

    lock.GetBody().SetIsSensor(rb.sensor_);

    JPH::MotionProperties* mp = lock.GetBody().GetMotionPropertiesUnchecked();
    if (!mp)
        return;

    mp->SetLinearDamping(rb.linearDamping_);
    mp->SetAngularDamping(rb.angularDamping_);
    mp->SetMassProperties(mp->GetAllowedDOFs(), massPropertiesOf(shape, rb.mass_));
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

void createBody(JPH::BodyInterface& bi, entt::entity e, RigidBody_C& rb, const Transform_C& tc,
                AssetManager& assets)
{
    rb.shapeScale_ = tc.scale();
    rb.dirty_ = false;
    rb.prevValid_ = false;
    JPH::ShapeRefC shape = makeShape(rb, rb.shapeScale_, assets);
    JPH::BodyCreationSettings settings(shape, toJolt(tc.pos()), toJolt(tc.rot()),
                                       motionTypeOf(rb.motion_), layerOf(rb.motion_));
    settings.mFriction = rb.friction_;
    settings.mRestitution = rb.restitution_;
    settings.mLinearDamping = rb.linearDamping_;
    settings.mAngularDamping = rb.angularDamping_;
    settings.mGravityFactor = rb.gravityFactor_;
    settings.mIsSensor = rb.sensor_;
    settings.mOverrideMassProperties = JPH::EOverrideMassProperties::MassAndInertiaProvided;
    settings.mMassPropertiesOverride = massPropertiesOf(*shape, rb.mass_);
    // Offset by one so that a destroyed body, whose GetUserData reads back 0,
    // never resolves to entity 0.
    settings.mUserData = static_cast<uint64_t>(entt::to_integral(e)) + 1u;

    rb.bodyId_ =
        bi.CreateAndAddBody(settings, activationOf(rb.motion_)).GetIndexAndSequenceNumber();
}

void rememberPose(JPH::BodyInterface& bi, JPH::BodyID id, RigidBody_C& rb)
{
    rb.prevValid_ = bi.IsActive(id);
    if (!rb.prevValid_)
        return;

    JPH::RVec3 pos;
    JPH::Quat rot;
    bi.GetPositionAndRotation(id, pos, rot);
    rb.prevPos_ = toEigen(pos);
    rb.prevRot_ = toEigen(rot);
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

void Physics_S::collectContacts(World& world)
{
    PhysicsWorld& physics = world.physics();
    JPH::BodyInterface& bi = physics.bodies();
    physics.contacts().take(rawContacts_);

    events_.clear();
    events_.reserve(rawContacts_.size());

    const auto entityOf = [&](uint32_t bodyId) -> EntityHandle
    {
        if (bodyId == kInvalidBodyId)
            return {};
        const uint64_t user = bi.GetUserData(JPH::BodyID{bodyId});
        if (user == 0)
            return {};
        return {&world.registry_, static_cast<entt::entity>(user - 1u)};
    };

    for (const RawContact& raw : rawContacts_)
    {
        ContactEvent ev;
        ev.a_ = entityOf(raw.bodyA_);
        ev.b_ = entityOf(raw.bodyB_);
        if (!ev.a_.valid() || !ev.b_.valid())
            continue;

        ev.entered_ = raw.entered_;
        ev.point_ = raw.point_;
        ev.normal_ = raw.normal_;
        ev.closingSpeed_ = raw.closingSpeed_;
        events_.push_back(ev);
    }
}

void Physics_S::drawColliders(World& world)
{
    if (!showColliders_)
        return;

    DebugDraw& dbg = world.debugOverlay();
    AssetManager& assets = world.assets();
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
                case Shape::Kind::Mesh:
                    if (const Mesh* mesh = assets.get(s.mesh_); mesh && mesh->localBounds_.valid())
                    {
                        const v3f center = (mesh->localBounds_.min_ + mesh->localBounds_.max_) * 0.5f;
                        const v3f half = (mesh->localBounds_.max_ - mesh->localBounds_.min_) * 0.5f;
                        drawMeshProxy(dbg,
                                      xform * TRS_Transform(center.cwiseProduct(scale),
                                                            quatf::Identity(), v3f::Ones()),
                                      half.cwiseProduct(scale), color);
                    }
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
    AssetManager& assets = world.assets();

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
            createBody(bi, e, rb, tc, assets);
            continue;
        }

        const JPH::BodyID id{rb.bodyId_};

        if (rb.dirty_ || !tc.scale().isApprox(rb.shapeScale_))
        {
            rb.dirty_ = false;
            rb.shapeScale_ = tc.scale();

            JPH::ShapeRefC shape = makeShape(rb, rb.shapeScale_, assets);
            bi.SetShape(id, shape, false, activationOf(rb.motion_));
            bi.SetFriction(id, rb.friction_);
            bi.SetRestitution(id, rb.restitution_);
            bi.SetGravityFactor(id, rb.gravityFactor_);
            applyLockedProperties(physics, id, rb, *shape);
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
                createBody(bi, e, rb, tc, assets);
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
        else
            rememberPose(bi, id, rb);
    }

    physics.step(dt);
    collectContacts(world);

    Transform_S& transforms = *world.systems().transforms_;
    for (auto e : view)
    {
        const auto& rb = view.get<RigidBody_C>(e);
        if (rb.motion_ != RigidBody_C::Motion::Dynamic || rb.bodyId_ == kInvalidBodyId)
            continue;

        const JPH::BodyID id{rb.bodyId_};
        // Asleep since before the step: its pose did not move. One that fell
        // asleep during the step still needs its final pose written.
        if (!rb.prevValid_ && !bi.IsActive(id))
            continue;

        const EntityHandle h{&reg, e};
        transforms.setLocalPosition(h, toEigen(bi.GetPosition(id)));
        transforms.setLocalRotation(h, toEigen(bi.GetRotation(id)));
    }
}

void Physics_S::interpolate(World& world, float alpha)
{
    auto& reg = world.registry_;
    JPH::BodyInterface& bi = world.physics().bodies();
    Transform_S& transforms = *world.systems().transforms_;

    auto view = reg.view<RigidBody_C, Transform_C>();
    for (auto e : view)
    {
        const auto& rb = view.get<RigidBody_C>(e);
        if (rb.motion_ != RigidBody_C::Motion::Dynamic || rb.bodyId_ == kInvalidBodyId ||
            !rb.prevValid_)
            continue;

        const JPH::BodyID id{rb.bodyId_};
        if (!bi.IsActive(id))
            continue;

        JPH::RVec3 pos;
        JPH::Quat rot;
        bi.GetPositionAndRotation(id, pos, rot);

        // Through Transform_S, not EntityHandle: its setters push the pose
        // back into Jolt.
        const EntityHandle h{&reg, e};
        transforms.setLocalPosition(h, rb.prevPos_ + (toEigen(pos) - rb.prevPos_) * alpha);
        transforms.setLocalRotation(h, rb.prevRot_.slerp(alpha, toEigen(rot)));
    }
}

}  // namespace batap
