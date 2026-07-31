#pragma once

#include "EntityDesc.h"

#include <string>
#include <vector>

namespace batap
{

struct World;
struct Engine;

struct EntitySerializer
{
    static void save(World& world, const Engine& ctx, const std::string& path);
    static void save(const std::vector<EntityDesc>& entities, const std::string& path);
    
    static void clearSceneAndLoad(World& world, const Engine& ctx, const std::string& path);
    static void instantiate(World& world, const Engine& ctx, const std::string& path);
};

}  // namespace batap
