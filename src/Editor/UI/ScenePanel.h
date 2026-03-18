#pragma once

#include "Components/EntityHandle.h"

#include <entt/entt.hpp>
#include <optional>

namespace batap
{
struct World;

struct ScenePanel
{
    void draw(World& world, std::optional<EntityHandle>& selectedEntity);

  private:
    void drawEntityNode(entt::registry& reg, entt::entity e,
                        std::optional<EntityHandle>& selectedEntity);
};
}  // namespace batap
