#pragma once

#include "Components/EntityHandle.h"
#include "UI/InspectorPanel.h"
#include "UI/ScenePanel.h"

#include <optional>
#include <string>

namespace batap
{
struct World;
struct App;
struct Engine;

struct UIPanels
{
    void draw(World& world, App& app, Engine& ctx);
    void drawStartupScreen(App& app, Engine& ctx);
    void clearSelection() { selectedEntity_.reset(); }
    void select(EntityHandle ent) { selectedEntity_ = ent; }

   private:
    void pickOnClick(World& world, App& app, Engine& ctx);
    void drawSelectionBounds(World& world, App& app, Engine& ctx);

    float panelWidth_ = 260.0f;

    std::optional<EntityHandle> selectedEntity_;
    std::string currentScenePath_;
    ScenePanel scenePanel_;
    InspectorPanel inspectorPanel_;
};
}  // namespace batap
