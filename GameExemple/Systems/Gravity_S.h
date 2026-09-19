#pragma once

#include "Components/CelestialBody_C.h"
#include "Components/Character_C.h"
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

            EntityHandle{&reg, e}.addForce(pull(sources, e, tc.pos()) * rb.mass_);
        }

        for (auto [e, ch, tc] : reg.view<Character_C, Transform_C>().each())
            ch.gravity_ = pull(sources, e, tc.pos());
    }

   private:
    template <typename View>
    static v3f pull(View& sources, entt::entity self, const v3f& at)
    {
        v3f acc = v3f::Zero();
        for (auto [se, cb, stc] : sources.each())
        {
            if (se == self)
                continue;

            const v3f d = stc.pos() - at;
            const float r2 = d.squaredNorm();
            if (r2 < 1e-6f)
                continue;

            acc += d * (cb.mu_ / (r2 * std::sqrt(r2)));
        }
        return acc;
    }
};

}  // namespace batap
