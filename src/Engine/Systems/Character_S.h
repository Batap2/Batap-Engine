#pragma once

#include <entt/entt.hpp>

namespace batap
{

struct World;

struct Character_S
{
    void connectHooks(entt::registry& reg);

    void fixedUpdate(World& world, float dt);

   private:
    void onCharacterDestroyed(entt::registry& reg, entt::entity e);
    void onCharacterChanged(entt::registry& reg, entt::entity e);
};

}  // namespace batap
