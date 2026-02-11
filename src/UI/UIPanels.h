#pragma once

#include "Context.h"

namespace batap
{
struct UIPanels
{
    UIPanels(Context& ctx);
    void Draw();
    void DrawLeftPanel();

    Context& _ctx;
};
}  // namespace batap
