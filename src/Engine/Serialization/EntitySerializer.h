#pragma once

#include "EntityDesc.h"

#include <string>
#include <vector>

namespace batap
{

struct World;
struct Context;

struct EntitySerializer
{
    static void save       (World& world, const Context& ctx, const std::string& path);
    static void load       (World& world, const Context& ctx, const std::string& path);
    static void instantiate(World& world, const Context& ctx, const std::string& path);

    // Write a .btpl / .bscene from raw descriptions (no World needed)
    static void saveTemplate(const std::vector<EntityDesc>& entities, const std::string& path);
};

}  // namespace batap
