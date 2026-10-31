#pragma once

#include <entt/entt.hpp>

namespace batap
{
struct World;

struct Audio_S
{
    void connectHooks(entt::registry& reg);
    void update(World& world, float dt);

   private:
    void onDestroy(entt::registry& reg, entt::entity e);
};
}  // namespace batap
