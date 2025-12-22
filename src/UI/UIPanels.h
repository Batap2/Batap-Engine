#pragma once

#include "Context.h"

namespace rayvox
{
struct UIPanels
{
    UIPanels(Context& ctx);
    void Draw();
    void DrawLeftPanel();

    Context& _ctx;
};
}  // namespace rayvox
