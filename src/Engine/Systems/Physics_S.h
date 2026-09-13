#pragma once

#include <entt/entt.hpp>

namespace batap
{

struct World;

struct Physics_S
{
    void connectHooks(entt::registry& reg);

    void fixedUpdate(World& world, float dt);

    void drawColliders(World& world);
    bool showColliders_ = false;

   private:
    void onRigidBodyDestroyed(entt::registry& reg, entt::entity e);
    void onRigidBodyChanged(entt::registry& reg, entt::entity e);
};
}  // namespace batap
