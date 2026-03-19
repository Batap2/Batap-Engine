#pragma once

#include "Components/EntityHandle.h"
#include "UI/InspectorPanel.h"
#include "UI/ScenePanel.h"

#include <optional>

namespace batap
{
struct World;
struct App;

struct UIPanels
{
    void draw(World& world, App& app);

   private:
    float panelWidth_ = 260.0f;

    std::optional<EntityHandle> selectedEntity_;
    ScenePanel scenePanel_;
    InspectorPanel inspectorPanel_;
};
}  // namespace batap
