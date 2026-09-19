#include "Camera_S.h"

#include "Components/Camera_C.h"

namespace batap
{

void Camera_S::connectHooks(entt::registry& reg)
{
    reg.on_update<Camera_C>().connect<&Camera_S::onCameraChanged>(*this);
}

// Two active cameras would leave the rendered one to entt's iteration order,
// so the last one activated wins and the others are cleared. Plain writes
// here: a patch would re-enter this hook.
void Camera_S::activate(entt::registry& reg, entt::entity e)
{
    for (auto [other, cam] : reg.view<Camera_C>().each())
        cam.active_ = (other == e);
}

void Camera_S::onCameraChanged(entt::registry& reg, entt::entity e)
{
    if (reg.get<Camera_C>(e).active_)
        activate(reg, e);
}

}  // namespace batap
