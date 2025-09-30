#pragma once

#include "Camera.h"

struct Scene{
    Camera Camera;

    void OnUpdate(float deltaTime);
};