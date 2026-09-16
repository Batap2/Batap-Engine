#pragma once

#include "Components/CelestialBody_C.h"
#include "Components/RigidBody_C.h"
#include "Components/Transform_C.h"
#include "batap.h"

#include <cmath>

namespace batap
{

struct Gravity_S
{
    void fixedUpdate(World& world)
    {
        auto& reg = world.registry_;

        auto sources = reg.view<CelestialBody_C, Transform_C>();
        auto pulled = reg.view<RigidBody_C, Transform_C>();

        for (auto [e, rb, tc] : pulled.each())
        {
            if (rb.motion_ != RigidBody_C::Motion::Dynamic)
                continue;

            v3f acc = v3f::Zero();
            for (auto [se, cb, stc] : sources.each())
            {
                if (se == e)
                    continue;

                const v3f d = stc.pos() - tc.pos();
                const float r2 = d.squaredNorm();
                if (r2 < 1e-6f)
                    continue;

                acc += d * (cb.mu_ / (r2 * std::sqrt(r2)));
            }

            EntityHandle{&reg, e}.addForce(acc * rb.mass_);
        }
    }
};

}  // namespace batap
