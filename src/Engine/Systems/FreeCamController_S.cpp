#include "FreeCamController_S.h"

#include "Components/FreeCamController_C.h"
#include "Engine.h"
#include "InputManager.h"
#include "World.h"

namespace batap
{
void FreeCamController_S::update(Engine& ctx, World& world, float deltaTime)
{
    world.registry_.view<Transform_C, FreeCamController_C>().each(
        [&](entt::entity ent, Transform_C& transform, FreeCamController_C& controller)
        {
            if (!controller.controlled_)
                return;

            if (controller.requireRightMouseButton_ &&
                !ctx.inputManager_->down(MouseButton::Right))
                return;

            entt::registry* reg = &world.registry_;

            const v2i mouseD = ctx.inputManager_->mouseDelta();
            if (!mouseD.isZero())
            {
                controller.yaw_ += static_cast<float>(-mouseD.x()) * controller.mouseSensitivity_;
                controller.pitch_ +=
                    static_cast<float>(-mouseD.y()) * controller.mouseSensitivity_;

                constexpr float pitchLimit = 1.55334306f;
                controller.pitch_ = std::clamp(controller.pitch_, -pitchLimit, pitchLimit);

                quatf qYaw{angleaxisf(controller.yaw_, v3f::UnitY())};
                quatf qPitch{angleaxisf(controller.pitch_, v3f::UnitX())};

                EntityHandle{reg, ent}.setLocalRotation((qYaw * qPitch).normalized());
            }

            float scroll = ctx.inputManager_->wheel();
            if (scroll != 0.f)
            {
                controller.moveSpeed_ *= std::pow(1.15f, scroll);
                controller.moveSpeed_  = std::clamp(controller.moveSpeed_, 0.1f, 1000.f);
                controller.boostSpeed_ = controller.moveSpeed_ * 3.f;
            }

            v3f move = v3f::Zero();

            if (ctx.inputManager_->down(Key::D))
                move.x() += 1.f;
            if (ctx.inputManager_->down(Key::A))
                move.x() -= 1.f;
            if (ctx.inputManager_->down(Key::E))
                move.y() += 1.f;
            if (ctx.inputManager_->down(Key::Q))
                move.y() -= 1.f;
            if (ctx.inputManager_->down(Key::W))
                move.z() -= 1.f;
            if (ctx.inputManager_->down(Key::S))
                move.z() += 1.f;

            if (move.squaredNorm() > 0.f)
            {
                move.normalize();

                float speed = ctx.inputManager_->down(Key::LShift) ? controller.boostSpeed_
                                                                        : controller.moveSpeed_;

                EntityHandle{reg, ent}.translate(move * speed * deltaTime, Space::Local);
            }
        });
}
}  // namespace batap
