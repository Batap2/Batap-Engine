#pragma once

#include <entt/entt.hpp>

namespace rayvox
{
struct Scene
{
    Scene();

    virtual void update(float deltaTime);

    entt::registry _registry;
};
}  // namespace rayvox