#pragma once

#include "Components/EntityHandle.h"

#include <entt/entt.hpp>
#include <optional>
#include <string>

namespace batap
{
struct World;

struct ScenePanel
{
    void draw(World& world, std::optional<EntityHandle>& selectedEntity);

  private:
    static constexpr float kRowMargin = 4.0f;

    void drawEntityNode(World& world, entt::entity e,
                        std::optional<EntityHandle>& selectedEntity);

    // Context menu actions are deferred to the end of draw(): creating or
    // destroying entities while iterating the registry storage is not safe.
    std::optional<EntityHandle> pendingDelete_;
    std::optional<EntityHandle> pendingDuplicate_;

    // Inline rename: the node being renamed draws an InputText instead.
    std::optional<EntityHandle> renaming_;
    std::string renameBuffer_;
    bool renameFocusPending_ = false;
};
}  // namespace batap
