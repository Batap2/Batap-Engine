#pragma once

#include "Components/Camera_C.h"
#include "Components/Carryable_C.h"
#include "Components/Grabber_C.h"
#include "Components/Transform_C.h"
#include "Systems/Hierarchy_S.h"
#include "batap.h"

namespace batap
{

struct Grab_S
{
    void update(World& world, float dt)
    {
        auto& reg = world.registry_;
        if (!world.input().pressed(Key::E))
            return;

        for (auto [e, gr] : reg.view<Grabber_C>().each())
        {
            if (gr.held_ != entt::null)
            {
                gr.held_ = entt::null;
                continue;
            }

            const Transform_C* eye = eyeOf(reg, e);
            if (!eye)
                continue;

            Ray ray;
            ray.origin_ = eye->world().translation();
            ray.dir_ = -eye->world().linear().col(2).normalized();
            ray.tMax_ = gr.reach_;

            const RayHit hit = world.raycastPhysics(ray);
            if (hit.hit() && reg.all_of<Carryable_C>(hit.entity_))
                gr.held_ = hit.entity_;
        }
    }

    void fixedUpdate(World& world, float dt)
    {
        auto& reg = world.registry_;

        for (auto [e, gr] : reg.view<Grabber_C>().each())
        {
            if (gr.held_ == entt::null)
                continue;

            EntityHandle obj{&reg, gr.held_};
            const Carryable_C* carry = obj.try_get<Carryable_C>();
            const Transform_C* body = obj.try_get<Transform_C>();
            const Transform_C* eye = eyeOf(reg, e);
            if (!carry || !body || !eye)
            {
                gr.held_ = entt::null;
                continue;
            }

            const v3f forward = -eye->world().linear().col(2).normalized();
            const v3f target = eye->world().translation() + forward * gr.holdDistance_;

            v3f force =
                (target - body->pos()) * gr.stiffness_ - obj.velocity() * gr.damping_;
            force *= carry->grip_;

            const float limit = gr.maxForce_ * carry->grip_;
            const float n = force.norm();
            if (n > limit && n > 0.f)
                force *= limit / n;

            obj.addForce(force);
        }
    }

   private:
    static const Transform_C* eyeOf(entt::registry& reg, entt::entity holder)
    {
        for (entt::entity c : Hierarchy_S::children({&reg, holder}))
            if (reg.all_of<Camera_C>(c))
                return reg.try_get<Transform_C>(c);
        return nullptr;
    }
};

}  // namespace batap
