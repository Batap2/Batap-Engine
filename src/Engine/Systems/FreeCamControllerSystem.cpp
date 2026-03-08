#include "FreeCamControllerSystem.h"

#include "Components/FreeCamController_C.h"
#include "Context.h"
#include "Systems/Systems.h"
#include "Systems/TransformSystem.h"
#include "InputManager.h"
#include "World.h"


namespace batap
{
void FreeCamControllerSystem::update(Context& ctx, World& world, float deltaTime)
{
    world.scene_->_registry.view<Transform_C, FreeCamController_C>().each(
        [&](entt::entity ent, Transform_C& transform, FreeCamController_C& controller)
        {
            if (!controller.controlled_)
                return;

            const bool looking = !controller.requireRightMouseButton_ ||
                                 ctx._inputManager->IsMouseButtonDown(MouseButton::Right);

            entt::registry* reg = &world.scene_->_registry;

            if (looking)
            {
                v2i mouseD = ctx._inputManager->GetMouseDelta();

                if(!mouseD.isZero()){
                    controller.yaw_ += static_cast<float>(-mouseD.x()) * controller.mouseSensitivity_;
                    controller.pitch_ += static_cast<float>(-mouseD.y()) * controller.mouseSensitivity_;
    
                    constexpr float pitchLimit = 1.55334306f;
                    controller.pitch_ = std::clamp(controller.pitch_, -pitchLimit, pitchLimit);
    
                    quatf qYaw{angleaxisf(controller.yaw_, v3f::UnitY())};
                    quatf qPitch{angleaxisf(controller.pitch_, v3f::UnitX())};
    
                    world.systems_->_transforms->setLocalRotation({reg, ent}, (qYaw * qPitch).normalized());
                }
            }

            v3f move = v3f::Zero();

            if (ctx._inputManager->IsKeyDown(Key::D))
                move.x() += 1.f;
            if (ctx._inputManager->IsKeyDown(Key::A))
                move.x() -= 1.f;
            if (ctx._inputManager->IsKeyDown(Key::E))
                move.y() += 1.f;
            if (ctx._inputManager->IsKeyDown(Key::Q))
                move.y() -= 1.f;
            if (ctx._inputManager->IsKeyDown(Key::W))
                move.z() -= 1.f;
            if (ctx._inputManager->IsKeyDown(Key::S))
                move.z() += 1.f;

            if (move.squaredNorm() > 0.f)
            {
                move.normalize();

                float speed = ctx._inputManager->IsKeyDown(Key::LShift) ? controller.boostSpeed_
                                                                           : controller.moveSpeed_;

                world.systems_->_transforms->translate({reg, ent}, move * speed * deltaTime, Space::Local);
            }
        });
}
}  // namespace batap
