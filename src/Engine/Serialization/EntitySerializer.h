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

    static bool saveTemplate(World& world, const Engine& ctx, const std::string& path);
    static std::string templateToBuffer(World& world, const Engine& ctx);

    // In-memory round-trip (editor Play/Stop): same json as the files, no disk.
    static std::string toBuffer(World& world, const Engine& ctx);
    static void clearSceneAndLoadBuffer(World& world, const Engine& ctx, const std::string& buffer);
};

}  // namespace batap
