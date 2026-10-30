#pragma once

#include <entt/entt.hpp>

namespace batap
{

struct World;

struct Character_S
{
    void connectHooks(entt::registry& reg);

    void fixedUpdate(World& world, float dt);

    // After the frame's fixed steps: the transform gets the position alpha of
    // the way from before the last step to after it, as Physics_S does for
    // bodies. The character itself keeps the real one.
    void interpolate(World& world, float alpha);
    // Before a fixed step: the real position back into the transform.
    void restorePoses(World& world);

   private:
    void onCharacterDestroyed(entt::registry& reg, entt::entity e);
    void onCharacterChanged(entt::registry& reg, entt::entity e);

    bool interpolated_ = false;
};

}  // namespace batap
