#pragma once

namespace batap
{
struct Engine;
struct World;

struct Billboard_S
{
    void submit(World& world, Engine& ctx);
};
}  // namespace batap
