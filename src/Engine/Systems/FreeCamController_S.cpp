#include "FreeCamController_S.h"

#include "Components/Camera_C.h"
#include "Components/FreeCamController_C.h"
#include "Engine.h"
#include "InputManager.h"
#include "Instance/InstanceManager.h"
#include "World.h"

namespace batap
{
namespace
{
constexpr float kOrthoLeavePixels = 4.f;
constexpr float kOrthoZoomRate = 1.5f;
constexpr float kOrthoPanRate = 0.75f;
constexpr float kOrthoBoost = 3.f;
}  // namespace

void FreeCamController_S::update(Engine& ctx, World& world, float deltaTime)
{
    world.registry_.view<Transform_C, FreeCamController_C>().each(
        [&](entt::entity ent, Transform_C& transform, FreeCamController_C& controller)
        {
            if (!controller.controlled_)
                return;

            const bool looking = !controller.requireRightMouseButton_ ||
                                 ctx.inputManager_->down(MouseButton::Right);

            entt::registry* reg = &world.registry_;
            EntityHandle handle{reg, ent};
            Camera_C* camera = reg->try_get<Camera_C>(ent);

            const v2i mouseD = ctx.inputManager_->mouseDelta();
            if (camera && camera->ortho())
            {
                // The keys move the view with the button held: a nudge of the
                // mouse on the way must not turn it.
                controller.orthoLook_ =
                    looking ? controller.orthoLook_ + mouseD.cast<float>().norm() : 0.f;
                if (controller.orthoLook_ > kOrthoLeavePixels)
                {
                    leaveFreeCamOrtho(handle, controller, *camera);
                    world.instances().markDirty<Camera_C>(handle);
                }
            }
            const bool ortho = camera && camera->ortho();

            if (looking && !mouseD.isZero() && !ortho)
            {
                controller.yaw_ += static_cast<float>(-mouseD.x()) * controller.mouseSensitivity_;
                controller.pitch_ += static_cast<float>(-mouseD.y()) * controller.mouseSensitivity_;
                controller.pitch_ =
                    std::clamp(controller.pitch_, -kFreeCamPitchLimit, kFreeCamPitchLimit);
            }

            const quatf rot = freeCamRotation(controller);
            if (!transform.rot().isApprox(rot))
                handle.setLocalRotation(rot);

            if (!looking)
                return;

            float scroll = ctx.inputManager_->wheel();
            if (scroll != 0.f && !ortho)
            {
                controller.moveSpeed_ *= std::pow(1.15f, scroll);
                controller.moveSpeed_ = std::clamp(controller.moveSpeed_, 0.1f, 1000.f);
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

                if (ortho)
                {
                    const float step =
                        (ctx.inputManager_->down(Key::LShift) ? kOrthoBoost : 1.f) * deltaTime;
                    handle.translate(v3f{move.x(), move.y(), 0.f} * camera->orthoHeight_ *
                                         kOrthoPanRate * step,
                                     Space::Local);
                    camera->orthoHeight_ = std::clamp(
                        camera->orthoHeight_ * std::exp(move.z() * kOrthoZoomRate * step),
                        kFreeCamOrthoMinHeight, kOrthoDepth);
                    world.instances().markDirty<Camera_C>(handle);
                    return;
                }

                float speed = ctx.inputManager_->down(Key::LShift) ? controller.boostSpeed_
                                                                   : controller.moveSpeed_;

                handle.translate(move * speed * deltaTime, Space::Local);
            }
        });
}
}  // namespace batap
