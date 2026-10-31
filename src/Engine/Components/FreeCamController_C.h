#pragma once

#include "EigenTypes.h"
#include "Reflection/ComponentRegistry.h"

namespace batap
{
struct FreeCamController_C
{
    float moveSpeed_ = 10.0f;
    float boostSpeed_ = 30.0f;
    float mouseSensitivity_ = 0.0025f;

    float yaw_ = 0.0f;
    float pitch_ = 0.0f;

    bool requireRightMouseButton_ = false;
    bool controlled_ = true;

    quatf frame_ = quatf::Identity();

    float orthoFocus_ = 0.0f;
    float orthoLook_ = 0.0f;
};

BATAP_COMPONENT(FreeCamController_C, "freeCamController",
                ComponentMeta{.color = ComponentColor::Cyan},
                fieldSkip<&FreeCamController_C::frame_>(),
                fieldSkip<&FreeCamController_C::orthoFocus_>(),
                fieldSkip<&FreeCamController_C::orthoLook_>());
}  // namespace batap
