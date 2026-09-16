#pragma once

#include "Components/RigidBody_C.h"
#include "Components/Rocket_C.h"
#include "Components/Transform_C.h"
#include "batap.h"

#include <algorithm>
#include <cmath>

namespace batap
{

struct Rocket_S
{
    void fixedUpdate(World& world, float dt)
    {
        auto& reg = world.registry_;

        for (auto [e, rk, rb, tc] : reg.view<Rocket_C, RigidBody_C, Transform_C>().each())
        {
            EntityHandle h{&reg, e};

            const float throttle =
                rk.fuel_ > 0.f ? std::clamp(rk.throttle_, 0.f, 1.f) : 0.f;
            rk.fuel_ = std::max(0.f, rk.fuel_ - rk.burnRate_ * throttle * dt);

            const float mass = rk.dryMass_ + rk.fuel_;
            if (std::fabs(mass - rb.mass_) > 1e-4f)
                h.setMass(mass);

            const quatf rot = tc.rot();

            if (throttle > 0.f)
                h.addForce(rot * v3f::UnitY() * (rk.maxThrust_ * throttle),
                           tc.pos() + rot * rk.enginePos_);

            const v3f input = rk.rcsInput_.cwiseMax(-1.f).cwiseMin(1.f);
            if (!input.isZero(1e-4f))
            {
                h.addTorque(rot * (input * rk.rcsTorque_));
                continue;
            }

            if (!rk.assist_)
                continue;

            // The autopilot fires the same thrusters, so it cannot pull harder
            // than they do.
            v3f counter = h.angularVelocity() * -rk.assistGain_;
            const float n = counter.norm();
            if (n > rk.rcsTorque_)
                counter *= rk.rcsTorque_ / n;
            h.addTorque(counter);
        }
    }
};

}  // namespace batap
