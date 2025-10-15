#pragma once

#include <entt/entt.hpp>

#include "Camera.h"

struct Scene{
    Camera Camera;

    void update(float deltaTime);

    entt::registry _registry;
};