#pragma once

#include <entt/entt.hpp>

namespace rayvox
{
struct Scene
{
    Scene() = default;
    virtual ~Scene() = default; 

    virtual void update(float deltaTime){}

    entt::registry _registry;
};
}  // namespace rayvox
