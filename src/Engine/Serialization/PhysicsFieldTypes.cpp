#include "PhysicsFieldTypes.h"

#include "Components/RigidBody_C.h"
#include "Reflection/ComponentRegistry.h"

#include <nlohmann/json.hpp>

#include <vector>

namespace batap
{
namespace
{

nlohmann::json vecToJson(const v3f& v)
{
    return {v.x(), v.y(), v.z()};
}

v3f vecFromJson(const nlohmann::json& in, const v3f& fallback)
{
    if (!in.is_array() || in.size() != 3)
        return fallback;
    return v3f{in[0].get<float>(), in[1].get<float>(), in[2].get<float>()};
}

nlohmann::json shapeToJson(const Shape& s)
{
    nlohmann::json j;
    j["kind"] = static_cast<uint32_t>(s.kind_);
    j["halfExtents"] = vecToJson(s.halfExtents_);
    j["radius"] = s.radius_;
    j["halfHeight"] = s.halfHeight_;
    j["localPos"] = vecToJson(s.localPos_);
    j["localRotDeg"] = vecToJson(s.localRotDeg_);
    return j;
}

Shape shapeFromJson(const nlohmann::json& j)
{
    Shape s;
    if (!j.is_object())
        return s;
    if (j.contains("kind"))
        s.kind_ = static_cast<Shape::Kind>(j["kind"].get<uint32_t>());
    if (j.contains("halfExtents"))
        s.halfExtents_ = vecFromJson(j["halfExtents"], s.halfExtents_);
    if (j.contains("radius"))
        s.radius_ = j["radius"].get<float>();
    if (j.contains("halfHeight"))
        s.halfHeight_ = j["halfHeight"].get<float>();
    if (j.contains("localPos"))
        s.localPos_ = vecFromJson(j["localPos"], s.localPos_);
    if (j.contains("localRotDeg"))
        s.localRotDeg_ = vecFromJson(j["localRotDeg"], s.localRotDeg_);
    return s;
}

}  // namespace

void registerPhysicsFieldTypes()
{
    using Shapes = std::vector<Shape>;

    auto& slot = fieldTypeSlot<Shapes>();
    slot.typeName = "Shape[]";

    slot.toJson = [](const void* f, nlohmann::json& out, const Engine&)
    {
        out = nlohmann::json::array();
        for (const Shape& s : *static_cast<const Shapes*>(f))
            out.push_back(shapeToJson(s));
    };

    slot.fromJson = [](void* f, const nlohmann::json& in, const Engine&)
    {
        auto& shapes = *static_cast<Shapes*>(f);
        shapes.clear();
        if (!in.is_array())
            return;
        shapes.reserve(in.size());
        for (const auto& j : in)
            shapes.push_back(shapeFromJson(j));
    };
}

}  // namespace batap
