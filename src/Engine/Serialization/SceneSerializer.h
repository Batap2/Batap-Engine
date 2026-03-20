#pragma once

#include <string>

namespace batap
{

struct World;
struct Context;

struct SceneSerializer
{
    static void save(World& world, Context& ctx, const std::string& path);
    static void load(World& world, Context& ctx, const std::string& path);
};

}  // namespace batap
