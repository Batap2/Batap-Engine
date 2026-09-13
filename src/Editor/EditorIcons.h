#pragma once

#include "Assets/AssetHandle.h"
#include "Handles.h"
#include "Bbox.h"
#include "Spatial/Ray.h"

#include <optional>

#include <cstdint>

namespace batap
{
struct Engine;
struct World;
struct ResourceManager;

struct EditorIcons
{
    ~EditorIcons();

    void draw(World& world, Engine& ctx);

    // Icons are pushed straight into Billboards, not held as components,
    // so the spatial index cannot see them: picking them is our job.
    RayHit raycast(World& world, const Ray& ray, float maxT) const;
    std::optional<AABB> boundsOf(World& world, entt::entity e) const;
    bool show_ = true;

   private:
    struct Icon
    {
        GPUResourceHandle texture_;
        uint32_t bindlessIndex_ = 0xFFFFFFFFu;
    };

    void load(Engine& ctx);

    Icon light_;
    Icon camera_;
    MaterialHandle material_;
    ResourceManager* resources_ = nullptr;
    bool loaded_ = false;
};
}  // namespace batap
