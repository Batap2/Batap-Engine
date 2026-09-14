#pragma once

#include "Components/Camera_C.h"
#include "Components/Mesh_C.h"
#include "Components/PointLight_C.h"
#include "Components/Skybox_C.h"
#include "Components/Transform_C.h"
#include "UI/IconsMaterialDesign.h"

#include <entt/entt.hpp>

#include <string_view>

namespace batap
{

// One line per spawnable entity: what the scene menu offers and which
// components the entity is born with. The factory and the scene panel read
// this table instead of naming a kind each.
struct Spawnable
{
    std::string_view id;
    const char* label;
    const char* icon;
    bool (*matches)(const entt::registry&, entt::entity);
    void (*emplace)(entt::registry&, entt::entity);
};

// The head component identifies the entity — it is the one an instance pool
// keys on — and the rest come along. The two callbacks follow from the list;
// the id is spelled out, not derived from Head's type name, so a class rename
// cannot silently change what spawnableFor("camera") looks up.
template <class Head, class... Rest>
constexpr Spawnable spawnable(std::string_view id, const char* label, const char* icon)
{
    return {id, label, icon,
            +[](const entt::registry& r, entt::entity e) { return r.all_of<Head>(e); },
            +[](entt::registry& r, entt::entity e)
            {
                r.emplace<Head>(e);
                (r.emplace<Rest>(e), ...);
            }};
}

inline constexpr Spawnable Spawnables[] = {
    {"empty", "Entity", ICON_MD_CATEGORY, nullptr, nullptr},

    spawnable<Mesh_C, Transform_C>("mesh", "Static Mesh", ICON_MD_HVAC),
    spawnable<Camera_C, Transform_C>("camera", "Camera", ICON_MD_VIDEOCAM),
    spawnable<PointLight_C, Transform_C>("pointLight", "Point Light", ICON_MD_LIGHTBULB),
    spawnable<Skybox_C>("skybox", "Skybox", ICON_MD_PANORAMA),
};

// Both lookups fall back on the empty entity, which is the one entry every
// caller can always spawn.
inline const Spawnable& spawnableFor(const entt::registry& reg, entt::entity e)
{
    for (const Spawnable& s : Spawnables)
        if (s.matches && s.matches(reg, e))
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
