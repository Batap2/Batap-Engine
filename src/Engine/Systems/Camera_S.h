#pragma once

#include <entt/entt.hpp>

namespace batap
{

struct Camera_S
{
    void connectHooks(entt::registry& reg);

    static void activate(entt::registry& reg, entt::entity e);

   private:
    void onCameraChanged(entt::registry& reg, entt::entity e);
};

}  // namespace batap
