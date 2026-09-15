#pragma once

#include "Components/EntityHandle.h"
#include "Selection.h"

#include <entt/entt.hpp>
#include <optional>
#include <string>
#include <vector>

namespace batap
{
struct World;

struct ScenePanel
{
    // The caller reserves what it draws below the tree; nesting the panel in a
    // child of its own would count the window padding twice.
    void draw(World& world, Selection& selection, float bottomReserve = 0.0f);

  private:
    static constexpr float kRowMargin = 4.0f;

    void drawEntityNode(World& world, entt::entity e, Selection& selection);
    bool drawRename(entt::registry& reg, EntityHandle ent);

    // Context menu actions are deferred to the end of draw(): creating or
    // destroying entities while iterating the registry storage is not safe.
    std::optional<EntityHandle> pendingDelete_;
    std::optional<EntityHandle> pendingDuplicate_;
    std::optional<EntityHandle> pendingRange_;

    // Draw order, so a shift-click range follows what the folding leaves visible.
    std::vector<entt::entity> rowOrder_;

    std::optional<EntityHandle> renaming_;
    std::string renameBuffer_;
    bool renameFocusPending_ = false;
};
}  // namespace batap
