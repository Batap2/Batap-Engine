#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include "Components/EntityHandle.h"
#include "Instance/InstanceManager.h"
#include "Physics/ContactEvent.h"
#include "Renderer/SceneBinding.h"

#include <entt/entt.hpp>

namespace batap
{

struct Engine;
struct Systems;
struct EntityFactory;
struct AssetManager;
struct SpatialIndex;
struct Spawnable;
struct Game;
struct DebugDraw;
struct Billboards;
struct InputManager;
struct PhysicsWorld;

struct Time
{
    float scale_ = 1.f;
    bool paused_ = false;
    float fixedDt_ = 1.f / 60.f;
    float accumulator_ = 0.f;
};

struct World
{
    World(Engine& engine);
    ~World();

    void update();
    void update(Game& game);

    InputManager& input();
    DebugDraw& debug();
    DebugDraw& debugOverlay();
    Billboards& billboards();
    SceneRenderArgs renderArgs();
    const std::vector<ContactEvent>& contacts() const;
    bool loadScene(const std::string& path);

    // Replaces the registry itself: its per-type storages hold code pointers
    // into a loaded game DLL and must not outlive it (hot reload).
    void resetScene();

    EntityHandle spawn(std::string_view spawnableId);
    EntityHandle spawn(const Spawnable& spawnable);
    void destroy(EntityHandle h);

    Systems& systems() { return *systems_; }
    PhysicsWorld& physics() { return *physics_; }
    GPUInstanceManager& instances() { return *instanceManager_; }
    SpatialIndex& spatialIndex();
    void markSpatialDirty();
    EntityFactory& factory() { return *entityFactory_; }

    entt::registry registry_;
    Time time_;

   private:
    std::unique_ptr<Systems> systems_;
    std::unique_ptr<PhysicsWorld> physics_;
    std::unique_ptr<GPUInstanceManager> instanceManager_;
    std::unique_ptr<EntityFactory> entityFactory_;
    std::unique_ptr<SpatialIndex> spatialIndex_;

    Engine* ctx_ = nullptr;
};
}  // namespace batap
