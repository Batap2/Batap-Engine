#pragma once

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
};

BATAP_COMPONENT(FreeCamController_C, "freeCamController",
                ComponentMeta{.color = ComponentColor::Cyan});
}  // namespace batap
