#pragma once

#include "Components/OrbitCam_C.h"
#include "Components/Rocket_C.h"
#include "Components/Transform_C.h"
#include "batap.h"

#include <algorithm>
#include <cmath>

namespace batap
{

struct OrbitCam_S
{
    void lateUpdate(World& world, float)
    {
        auto& reg = world.registry_;
        InputManager& in = world.input();

        entt::entity target = entt::null;
        for (entt::entity e : reg.view<Rocket_C, Transform_C>())
        {
            target = e;
            break;
        }
        if (target == entt::null)
            return;

        const v3f focus = reg.get<Transform_C>(target).world().translation();

        for (entt::entity e : reg.view<OrbitCam_C, Transform_C>())
        {
            OrbitCam_C& oc = reg.get<OrbitCam_C>(e);
            if (in.down(MouseButton::Right))
            {
                const v2i d = in.mouseDelta();
                oc.yaw_ -= static_cast<float>(d.x()) * oc.mouseSensitivity_;

                constexpr float kPitchLimit = 1.55f;
                oc.pitch_ = std::clamp(oc.pitch_ - static_cast<float>(d.y()) * oc.mouseSensitivity_,
                                       -kPitchLimit, kPitchLimit);
            }

            const float wheel = in.wheel();
            if (wheel != 0.f)
                oc.distance_ = std::clamp(oc.distance_ * std::pow(oc.zoomStep_, -wheel),
                                          oc.minDistance_, oc.maxDistance_);

            const quatf rot =
                (angleaxisf(oc.yaw_, v3f::UnitY()) * angleaxisf(oc.pitch_, v3f::UnitX()))
                    .normalized();

            EntityHandle h{&reg, e};
            h.setPosition(focus + rot * (v3f::UnitZ() * oc.distance_), Space::World);
            h.setRotation(rot, Space::World);
        }
    }
};

}  // namespace batap
