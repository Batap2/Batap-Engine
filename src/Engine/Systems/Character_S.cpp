#include "Systems/Character_S.h"

#include "Physics/PhysicsWorld.h"

#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>

#include "Components/Character_C.h"
#include "Components/Transform_C.h"
#include "Physics/JoltConvert.h"
#include "Systems/Systems.h"
#include "Systems/Transform_S.h"
#include "World.h"

#include <numbers>

namespace batap
{
namespace
{

JPH::RefConst<JPH::Shape> makeShape(const Character_C& ch)
{
    // Jolt wants the bottom of a character shape at the origin, so the entity
    // transform sits at the feet rather than the middle of the capsule.
    return JPH::RotatedTranslatedShapeSettings(
               JPH::Vec3(0.f, ch.halfHeight_ + ch.radius_, 0.f), JPH::Quat::sIdentity(),
               new JPH::CapsuleShape(ch.halfHeight_, ch.radius_))
        .Create()
        .Get();
}

v3f upOf(const Character_C& ch)
{
    const float len = ch.gravity_.norm();
    return len > 1e-6f ? v3f(-ch.gravity_ / len) : v3f::UnitY();
}

JPH::CharacterVirtual& makeCharacter(PhysicsWorld& physics, uint32_t key, const Character_C& ch,
                                     const Transform_C& tc)
{
    constexpr float kDegToRad = std::numbers::pi_v<float> / 180.f;

    JPH::CharacterVirtualSettings settings;
    settings.mShape = makeShape(ch);
    settings.mUp = toJolt(upOf(ch));
    settings.mMaxSlopeAngle = ch.maxSlopeDeg_ * kDegToRad;
    settings.mMass = ch.mass_;
    settings.mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -ch.radius_);

    return physics.createCharacter(key, settings, toJolt(tc.pos()), toJolt(tc.rot()));
}

}  // namespace

void Character_S::connectHooks(entt::registry& reg)
{
    reg.on_destroy<Character_C>().connect<&Character_S::onCharacterDestroyed>(*this);
    reg.on_update<Character_C>().connect<&Character_S::onCharacterChanged>(*this);
}

void Character_S::onCharacterChanged(entt::registry& reg, entt::entity e)
{
    reg.get<Character_C>(e).dirty_ = true;
}

void Character_S::onCharacterDestroyed(entt::registry& reg, entt::entity e)
{
    World** world = reg.ctx().find<World*>();
    if (!world)
        return;

    (*world)->physics().destroyCharacter(entt::to_integral(e));
}

void Character_S::fixedUpdate(World& world, float dt)
{
    constexpr float kDegToRad = std::numbers::pi_v<float> / 180.f;

    auto& reg = world.registry_;
    PhysicsWorld& physics = world.physics();
    Transform_S& transforms = *world.systems().transforms_;

    for (auto [e, ch, tc] : reg.view<Character_C, Transform_C>().each())
    {
        const uint32_t key = entt::to_integral(e);

        JPH::CharacterVirtual* cv = physics.character(key);
        if (cv && ch.dirty_)
        {
            physics.destroyCharacter(key);
            cv = nullptr;
        }
        if (!cv)
            cv = &makeCharacter(physics, key, ch, tc);
        ch.dirty_ = false;

        cv->SetMaxSlopeAngle(ch.maxSlopeDeg_ * kDegToRad);
        cv->SetMass(ch.mass_);

        const v3f up = upOf(ch);
        cv->SetUp(toJolt(up));
        cv->SetRotation(toJolt(quatf::FromTwoVectors(v3f::UnitY(), up)));

        JPH::CharacterVirtual::ExtendedUpdateSettings update;
        v3f groundPush = v3f::Zero();

        if (ch.mode_ == Character_C::Mode::Fly)
        {
            cv->SetLinearVelocity(toJolt(ch.moveVelocity_));
            update.mStickToFloorStepDown = JPH::Vec3::sZero();
            update.mWalkStairsStepUp = JPH::Vec3::sZero();
        }
        else
        {
            const v3f velocity = toEigen(cv->GetLinearVelocity());
            const v3f groundVelocity = toEigen(cv->GetGroundVelocity());
            const v3f verticalVelocity = up * velocity.dot(up);

            const bool onGround =
                cv->GetGroundState() == JPH::CharacterBase::EGroundState::OnGround;
            const bool towardGround = (verticalVelocity - groundVelocity).dot(up) < 0.1f;

            v3f wanted = onGround && towardGround ? groundVelocity : verticalVelocity;
            if (onGround && towardGround && ch.wantJump_)
                wanted += up * ch.jumpSpeed_;
            wanted += ch.gravity_ * dt;
            wanted += ch.moveVelocity_ - up * ch.moveVelocity_.dot(up);
            cv->SetLinearVelocity(toJolt(wanted));

            // Jolt's defaults hard-code these two along +Y, which walks stairs
            // and sticks to the floor sideways as soon as gravity points
            // anywhere else.
            update.mStickToFloorStepDown = toJolt(v3f(-up * 0.5f));
            update.mWalkStairsStepUp = toJolt(v3f(up * 0.4f));
            groundPush = ch.gravity_;
        }

        JPH::PhysicsSystem& system = physics.system();
        cv->ExtendedUpdate(dt, toJolt(groundPush), update,
                           system.GetDefaultBroadPhaseLayerFilter(objectLayers::Moving),
                           system.GetDefaultLayerFilter(objectLayers::Moving), {}, {},
                           physics.tempAllocator());

        ch.onGround_ = cv->GetGroundState() == JPH::CharacterBase::EGroundState::OnGround;
        ch.velocity_ = toEigen(cv->GetLinearVelocity());
        ch.wantJump_ = false;

        transforms.setLocalPosition({&reg, e}, toEigen(cv->GetPosition()));
    }
}

}  // namespace batap
