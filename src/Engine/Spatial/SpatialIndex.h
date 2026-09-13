#pragma once

#include "Bbox.h"
#include "Renderer/Billboards.h"
#include "Spatial/BVH.h"
#include "Spatial/Ray.h"

#include <entt/entt.hpp>

#include <optional>
#include <vector>

namespace batap
{

struct Engine;
struct World;

struct SpatialIndex
{
    void markDirty() { dirty_ = true; }
    void onMeshChanged(entt::registry&, entt::entity) { dirty_ = true; }
    void refresh(World& world, Engine& ctx);
    void connectHooks(entt::registry& reg);

    RayHit raycast(const Ray& ray) const;
    void overlap(const AABB& box, std::vector<entt::entity>& out) const;
    void overlap(const v3f& center, float radius, std::vector<entt::entity>& out) const;

    size_t size() const { return entities_.size(); }

   private:
    RayHit raycastBillboards(const Ray& ray, float bestT) const;

    std::vector<entt::entity> entities_;
    std::vector<AABB> bounds_;
    BVH bvh_;
    World* world_ = nullptr;
    Engine* ctx_ = nullptr;
    bool dirty_ = true;
};

Ray rayFromScreen(World& world, Engine& ctx, const v2i& screenPos);

std::optional<CameraBasis> activeCameraBasis(entt::registry& reg);

// Bounds of what is actually drawn: the mesh, or the billboard quad — which
// faces the camera, so those bounds move with it.
std::optional<AABB> entityBounds(World& world, Engine& ctx, entt::entity e);

}  // namespace batap
