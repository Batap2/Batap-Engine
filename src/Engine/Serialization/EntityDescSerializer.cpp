#include "EntityDescSerializer.h"

#include "Components/Transform_C.h"

#include <type_traits>
#include <variant>

namespace batap
{

static nlohmann::json toJson(const v3f& v)
{
    return {v.x(), v.y(), v.z()};
}

static nlohmann::json toJson(const quatf& q)
{
    return {q.x(), q.y(), q.z(), q.w()};
}

// Keys mirror Transform_C::registerReflection().
nlohmann::json toJson(const Transform_C& c)
{
    return {{"pos", toJson(c.pos())}, {"rot", toJson(c.rot())}, {"scale", toJson(c.scale())}};
}

// Key mirrors Mesh_C::_mesh.
nlohmann::json toJson(const MeshDesc& d)
{
    return {{"mesh", d.path.empty() ? nlohmann::json(nullptr) : nlohmann::json(d.path)}};
}

// Keys mirror Materials_C. The slot array is always written full-length,
// null-padded, so a slot's index survives the round trip.
nlohmann::json toJson(const MaterialsDesc& d)
{
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& p : d.paths)
        arr.push_back(p.empty() ? nlohmann::json(nullptr) : nlohmann::json(p));
    return {{"slots", std::move(arr)}, {"count", d.count}};
}

std::string_view componentTypeName(const ComponentDesc& d)
{
    return std::visit(
        [](const auto& c) -> std::string_view
        {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, Transform_C>)
                return "transform";
            if constexpr (std::is_same_v<T, MeshDesc>)
                return "mesh";
            if constexpr (std::is_same_v<T, MaterialsDesc>)
                return "materials";
            return "";
        },
        d);
}

}  // namespace batap
