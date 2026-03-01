#pragma once

#include <memory>
#include "Scene.h"

namespace batap
{

struct Context;
struct Systems;
struct GPUInstanceManager;
struct EntityFactory;

struct World
{
    std::unique_ptr<Scene> scene_;
    std::unique_ptr<Systems> systems_;
    std::unique_ptr<GPUInstanceManager> instanceManager_;
    std::unique_ptr<EntityFactory> entityFactory_;

    World(Context& ctx);
    ~World();

    void update(Context& ctx);
};
}  // namespace batap
