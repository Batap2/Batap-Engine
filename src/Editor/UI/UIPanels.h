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
struct Context;

struct UIPanels
{
    void draw(World& world, App& app, Context& ctx);
    void drawStartupScreen(App& app, Context& ctx);

   private:
    float panelWidth_ = 260.0f;

    std::optional<EntityHandle> selectedEntity_;
    std::string currentScenePath_;
    ScenePanel scenePanel_;
    InspectorPanel inspectorPanel_;
};
}  // namespace batap
