#pragma once

namespace batap
{

struct Engine;
struct World;

struct Bounds_S
{
    bool showBounds_ = false;

    void drawBounds(World& world, Engine& ctx);
};

}  // namespace batap
