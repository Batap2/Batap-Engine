#pragma once

#include <memory>
#include <string>
#include "Scene.h"

namespace batap
{

struct Engine;
struct Systems;
struct GPUInstanceManager;
struct EntityFactory;
struct AssetManager;

struct World
{
    World(Engine& engine);
    ~World();

    // One simulation step. Skipping it pauses the world; the engine keeps
    // presenting either way.
    void update();

    // Replaces the current scene with a .btpl. Relative paths resolve
    // against the project dir (Engine::setProjectDir). False when no
    // project dir was set or the file is missing.
    bool loadScene(const std::string& path);

    std::unique_ptr<Scene> scene_;
    std::unique_ptr<Systems> systems_;
    std::unique_ptr<GPUInstanceManager> instanceManager_;
    std::unique_ptr<EntityFactory> entityFactory_;

   private:
    Engine* ctx_ = nullptr;
};
}  // namespace batap
