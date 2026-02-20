#pragma once

#include "Components/EntityHandle.h"
#include "Context.h"

#include <optional>

namespace batap
{
struct UIPanels
{
    UIPanels(Context& ctx);
    void draw();
    void drawLeftPanel();
    void drawRegistryTree(entt::registry &reg);

    std::optional<EntityHandle> _currentlyClickedSelectedEntity;

    void drawEntityMenu();

    Context& _ctx;
};
}  // namespace batap
