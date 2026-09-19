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

// The gizmo's precision drag moves the pointer by a hundredth of a pixel:
// rounding to whole pixels swallows it entirely.
Ray rayFromScreen(World& world, Engine& ctx, const v2f& screenPos);
Ray rayFromScreen(World& world, Engine& ctx, const v2i& screenPos);

struct ScreenProjector
{
    m4f viewProj_ = m4f::Identity();
    v2f frameSize_ = v2f::Zero();
    v3f camPos_ = v3f::Zero();
    v3f camRight_ = v3f::UnitX();
    v3f camUp_ = v3f::UnitY();

    std::optional<v2f> project(const v3f& world) const;
};

std::optional<ScreenProjector> screenProjector(World& world, Engine& ctx);

std::optional<CameraBasis> renderCameraBasis(entt::registry& reg);

// Bounds of what is actually drawn: the mesh, or the billboard quad — which
// faces the camera, so those bounds move with it.
std::optional<AABB> entityBounds(World& world, Engine& ctx, entt::entity e);

}  // namespace batap
