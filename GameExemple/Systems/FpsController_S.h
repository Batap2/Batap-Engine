#pragma once

#include "Components/Camera_C.h"
#include "Components/Character_C.h"
#include "Components/FpsController_C.h"
#include "Components/Transform_C.h"
#include "Systems/Hierarchy_S.h"
#include "batap.h"

#include <algorithm>
#include <cmath>

namespace batap
{

struct FpsController_S
{
    void update(World& world, float dt)
    {
        auto& reg = world.registry_;
        InputManager& in = world.input();
        const v2i mouse = in.mouseDelta();

        for (auto [e, fc, ch] : reg.view<FpsController_C, Character_C>().each())
        {
            const v3f up = upOf(ch);
            v3f fwd = flatten(fc.forward_, up);

            fwd = angleaxisf(-static_cast<float>(mouse.x()) * fc.mouseSensitivity_, up) * fwd;
            fc.forward_ = fwd;

            constexpr float kPitchLimit = 1.55f;
            fc.pitch_ = std::clamp(
                fc.pitch_ - static_cast<float>(mouse.y()) * fc.mouseSensitivity_, -kPitchLimit,
                kPitchLimit);

            const v3f right = fwd.cross(up).normalized();

            v3f wish = v3f::Zero();
            if (in.down(Key::W))
                wish += fwd;
            if (in.down(Key::S))
                wish -= fwd;
            if (in.down(Key::D))
                wish += right;
            if (in.down(Key::A))
                wish -= right;
            if (!wish.isZero(1e-6f))
                wish.normalize();

            ch.moveVelocity_ = wish * fc.walkSpeed_;
            if (in.pressed(Key::Space))
                ch.wantJump_ = true;
        }
    }

    void lateUpdate(World& world, float dt)
    {
        auto& reg = world.registry_;

        for (auto [e, fc, ch, tc] : reg.view<FpsController_C, Character_C, Transform_C>().each())
        {
            const v3f up = upOf(ch);
            const v3f fwd = flatten(fc.forward_, up);
            const v3f right = fwd.cross(up).normalized();
            const v3f look = angleaxisf(fc.pitch_, right) * fwd;

            m3f basis;
            basis.col(2) = -look.normalized();
            basis.col(0) = right;
            basis.col(1) = basis.col(2).cross(right);

            for (entt::entity c : Hierarchy_S::children({&reg, e}))
            {
                if (!reg.all_of<Camera_C>(c))
                    continue;

                EntityHandle eye{&reg, c};
                eye.setPosition(tc.pos() + up * fc.eyeHeight_, Space::World);
                eye.setRotation(quatf(basis), Space::World);
                break;
            }
        }
    }

   private:
    static v3f upOf(const Character_C& ch)
    {
        const float len = ch.gravity_.norm();
        return len > 1e-6f ? v3f(-ch.gravity_ / len) : v3f::UnitY();
    }

    static v3f flatten(const v3f& v, const v3f& up)
    {
        v3f flat = v - up * v.dot(up);
        if (flat.squaredNorm() < 1e-8f)
            flat = up.cross(v3f::UnitX()).squaredNorm() > 1e-6f ? up.cross(v3f::UnitX())
                                                               : up.cross(v3f::UnitZ());
        return flat.normalized();
    }
};

}  // namespace batap
