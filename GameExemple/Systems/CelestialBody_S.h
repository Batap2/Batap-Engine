#pragma once

#include "Components/CelestialBody_C.h"
#include "Components/RigidBody_C.h"
#include "Components/Transform_C.h"
#include "batap.h"

#include <cmath>
#include <vector>

namespace batap
{

// Astres: their collider is derived from CelestialBody_C rather than authored.
// Kinematic is the point — Jolt never lets a contact move it, so nothing that
// lands on an astre shoves it, yet this system can still integrate its orbit
// and Physics_S carries whatever rests on it along. Dynamic would be pushed
// around by every body it attracts, so it is refused.
struct CelestialBody_S
{
    void fixedUpdate(World& world, float dt)
    {
        auto& reg = world.registry_;
        ensureColliders(reg);
        circularize(reg);
        integrate(reg, dt);
    }

   private:
    static void ensureColliders(entt::registry& reg)
    {
        for (auto [e, cb] : reg.view<CelestialBody_C>().each())
        {
            Shape wanted;
            wanted.kind_ = Shape::Kind::Sphere;
            wanted.radius_ = cb.radius_;

            RigidBody_C* rb = reg.try_get<RigidBody_C>(e);
            if (!rb)
            {
                RigidBody_C made;
                made.motion_ = RigidBody_C::Motion::Kinematic;
                made.shapes_ = {wanted};
                reg.emplace<RigidBody_C>(e, made);
                continue;
            }

            const bool badShape =
                rb->shapes_.size() != 1 || rb->shapes_[0].kind_ != Shape::Kind::Sphere ||
                std::fabs(rb->shapes_[0].radius_ - cb.radius_) > cb.radius_ * 1e-6f;
            const bool badMotion = rb->motion_ == RigidBody_C::Motion::Dynamic;

            if (!badShape && !badMotion)
                continue;

            reg.patch<RigidBody_C>(e,
                                   [&](RigidBody_C& r)
                                   {
                                       r.shapes_ = {wanted};
                                       if (r.motion_ == RigidBody_C::Motion::Dynamic)
                                           r.motion_ = RigidBody_C::Motion::Kinematic;
                                   });
        }
    }

    static void circularize(entt::registry& reg)
    {
        auto all = reg.view<CelestialBody_C, Transform_C>();

        for (auto [e, cb, tc] : all.each())
        {
            if (!cb.circularize_)
                continue;
            cb.circularize_ = false;

            entt::entity best = entt::null;
            float bestPull = 0.f;
            for (auto [oe, ocb, otc] : all.each())
            {
                if (oe == e)
                    continue;
                const float r2 = (otc.pos() - tc.pos()).squaredNorm();
                if (r2 < 1e-6f)
                    continue;
                const float pull = ocb.mu_ / r2;
                if (pull > bestPull)
                {
                    bestPull = pull;
                    best = oe;
                }
            }
            if (best == entt::null)
                continue;

            const auto& pcb = reg.get<CelestialBody_C>(best);
            const v3f d = tc.pos() - reg.get<Transform_C>(best).pos();
            const float r = d.norm();

            // Any direction square to d orbits; the one square to world up
            // keeps the default orbit flat, and the author tilts it from there.
            v3f dir = v3f::UnitY().cross(d);
            if (dir.squaredNorm() < 1e-6f)
                dir = v3f::UnitX().cross(d);

            cb.velocity_ = pcb.velocity_ + dir.normalized() * std::sqrt(pcb.mu_ / r);
        }
    }

    void integrate(entt::registry& reg, float dt)
    {
        auto moving = reg.view<CelestialBody_C, RigidBody_C, Transform_C>();
        auto sources = reg.view<CelestialBody_C, Transform_C>();

        moved_.clear();

        for (auto [e, cb, rb, tc] : moving.each())
        {
            if (rb.motion_ != RigidBody_C::Motion::Kinematic)
                continue;

            v3f acc = v3f::Zero();
            for (auto [oe, ocb, otc] : sources.each())
            {
                if (oe == e)
                    continue;
                const v3f d = otc.pos() - tc.pos();
                const float r2 = d.squaredNorm();
                if (r2 < 1e-6f)
                    continue;
                acc += d * (ocb.mu_ / (r2 * std::sqrt(r2)));
            }

            cb.velocity_ += acc * dt;
            moved_.push_back({e, tc.pos() + cb.velocity_ * dt});
        }

        // Positions move only once every acceleration is known, or the bodies
        // read each other half a step apart.
        for (const Step& s : moved_)
            EntityHandle{&reg, s.entity_}.setPosition(s.pos_);
    }

    struct Step
    {
        entt::entity entity_ = entt::null;
        v3f pos_ = v3f::Zero();
    };
    std::vector<Step> moved_;
};

}  // namespace batap
