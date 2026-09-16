#pragma once

#include "Components/Rotator_C.h"
#include "Components/Transform_C.h"
#include "Systems/CelestialBody_S.h"
#include "Systems/Gravity_S.h"
#include "Systems/Rocket_S.h"
#include "batap.h"

namespace batap
{
struct MyGame : Game
{
    CelestialBody_S bodies_;
    Gravity_S gravity_;
    Rocket_S rockets_;

    void init(World& world) override
    {
        // Every attraction comes from a CelestialBody_C, so Jolt's constant
        // world gravity would add a second one underneath.
        world.setGravity(v3f::Zero());
    }

    void fixedUpdate(World& world, float dt) override
    {
        bodies_.fixedUpdate(world, dt);
        gravity_.fixedUpdate(world);
        rockets_.fixedUpdate(world, dt);
    }

    void update(World& world, float dt) override
    {
        auto& reg = world.registry_;
        for (auto [e, rot] : reg.view<Rotator_C>().each())
        {
            EntityHandle h{&reg, e};
            auto* t = h.try_get<Transform_C>();
            if (!t)
                continue;
            const quatf q = (t->rot() * angleaxisf(rot.speed_ * dt, v3f::UnitY())).normalized();
            h.setLocalRotation(q);
        }
    }
};
}  // namespace batap
