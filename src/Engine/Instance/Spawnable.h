#pragma once

#include "Components/Camera_C.h"
#include "Components/Mesh_C.h"
#include "Components/PointLight_C.h"
#include "Components/Skybox_C.h"
#include "Components/Transform_C.h"
#include "Instance/EntityKind.h"
#include "UI/IconsMaterialDesign.h"

#include <entt/entt.hpp>

#include <string_view>

namespace batap
{

// One line per spawnable entity: what the scene menu offers, what a scene file
// writes as "kind", and which components the entity is born with. The factory,
// the serializer and the scene panel all read this table instead of naming a
// kind each.
struct Spawnable
{
    std::string_view id;
    const char* label;
    const char* icon;
    EntityKind kind;
    void (*emplace)(entt::registry&, entt::entity);
};

inline constexpr Spawnable Spawnables[] = {
    {"empty", "Entity", ICON_MD_CATEGORY, EntityKind::Empty, nullptr},

    {"mesh", "Static Mesh", ICON_MD_HVAC, EntityKind::StaticMesh,
     +[](entt::registry& r, entt::entity e)
     {
         r.emplace<Mesh_C>(e);
         r.emplace<Transform_C>(e);
     }},

    {"camera", "Camera", ICON_MD_VIDEOCAM, EntityKind::Camera,
     +[](entt::registry& r, entt::entity e)
     {
         r.emplace<Camera_C>(e);
         r.emplace<Transform_C>(e);
     }},

    {"pointLight", "Point Light", ICON_MD_LIGHTBULB, EntityKind::PointLight,
     +[](entt::registry& r, entt::entity e)
     {
         r.emplace<PointLight_C>(e);
         r.emplace<Transform_C>(e);
     }},

    {"skybox", "Skybox", ICON_MD_PANORAMA, EntityKind::Skybox,
     +[](entt::registry& r, entt::entity e) { r.emplace<Skybox_C>(e); }},
};

// Both lookups fall back on the empty entity, which is the one entry every
// caller can always spawn.
inline const Spawnable& spawnableFor(EntityKind kind)
{
    for (const Spawnable& s : Spawnables)
        if (s.kind == kind)
            return s;
    return Spawnables[0];
}

inline const Spawnable& spawnableFor(std::string_view id)
{
    for (const Spawnable& s : Spawnables)
        if (s.id == id)
            return s;
    return Spawnables[0];
}

}  // namespace batap
