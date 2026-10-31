#pragma once

#include "Components/EntityHandle.h"
#include "Selection.h"

#include <entt/entt.hpp>
#include <optional>
#include <string>
#include <vector>

namespace batap
{
struct App;
struct World;
struct Shortcuts;

inline constexpr const char* kEntityPayload = "ENTITY";

void sortByName(entt::registry& reg, std::vector<entt::entity>& entities);
EntityHandle duplicateEntity(World& world, App& app, EntityHandle src);

struct ScenePanel
{
    // The caller reserves what it draws below the tree; nesting the panel in a
    // child of its own would count the window padding twice.
    void draw(World& world, App& app, Selection& selection, float bottomReserve = 0.0f);
    void beginRename(EntityHandle ent);

    struct ContentDrop
    {
        std::string path_;
        EntityHandle parent_;
    };

   private:
    static constexpr float kRowMargin = 4.0f;

    void drawEntityNode(World& world, entt::entity e, Selection& selection,
                        const Shortcuts& shortcuts);
    bool drawRename(entt::registry& reg, EntityHandle ent);

    // Context menu actions are deferred to the end of draw(): creating or
    // destroying entities while iterating the registry storage is not safe.
    std::optional<EntityHandle> pendingDelete_;
    std::optional<EntityHandle> pendingDuplicate_;
    std::optional<EntityHandle> pendingRange_;
    std::optional<EntityHandle> pendingFocus_;
    std::optional<ContentDrop> pendingDrop_;
    std::optional<EntityHandle> pendingUnlink_;
    std::optional<EntityHandle> pendingRevert_;
    std::string pendingOpenTemplate_;

    // Draw order, so a shift-click range follows what the folding leaves visible.
    std::vector<entt::entity> rowOrder_;

    std::optional<EntityHandle> renaming_;
    std::string renameBuffer_;
    bool renameFocusPending_ = false;
};
}  // namespace batap
