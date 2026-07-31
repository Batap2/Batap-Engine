#include "ComponentRegistry.h"

#include "DebugUtils.h"
#include "EigenTypes.h"

#include <nlohmann/json.hpp>

#include <string>

namespace batap
{

// Compile-time proof that field discovery and name extraction work on this
// compiler — breaks the build instead of silently producing wrong json keys.
namespace
{
struct ReflProbeTest
{
    float alpha = 0.f;
    bool beta_ = false;        // trailing '_' must be stripped
    std::string gamma_;        // member with non-trivial ctor
};
static_assert(refl::fieldCount<ReflProbeTest>() == 3);
static_assert(refl::fieldName<ReflProbeTest, 0>() == "alpha");
static_assert(refl::fieldName<ReflProbeTest, 1>() == "beta");
static_assert(refl::fieldName<ReflProbeTest, 2>() == "gamma");
}  // namespace

ComponentRegistry& ComponentRegistry::instance()
{
    // Function-local so component headers registering at static init never
    // race it. Lives until process exit on purpose.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wexit-time-destructors"
    static ComponentRegistry registry;
#pragma clang diagnostic pop
    return registry;
}

void ComponentRegistry::add(ComponentType type)
{
    ThrowAssert(find(type.name) == nullptr,
                "component registered twice: " + type.name);
    types_.push_back(std::move(type));
}

const ComponentType* ComponentRegistry::find(std::string_view name) const
{
    for (const auto& t : types_)
        if (t.name == name)
            return &t;
    return nullptr;
}

void ComponentRegistry::validate() const
{
    for (const auto& t : types_)
        for (const auto& f : t.fields)
            ThrowAssert(f.type->toJson && f.type->fromJson,
                        "component '" + t.name + "' field '" + f.name +
                            "' has an unregistered field type (" + f.type->typeName +
                            ") — add it to registerBuiltinFieldTypes or register it yourself");
}

// --- builtin field types -------------------------------------------------

namespace
{
template <class M>
void setPlain(const char* typeName)
{
    auto& slot = fieldTypeSlot<M>();
    slot.typeName = typeName;
    slot.toJson = [](const void* f, nlohmann::json& out, const Engine&)
    { out = *static_cast<const M*>(f); };
    slot.fromJson = [](void* f, const nlohmann::json& in, const Engine&)
    { *static_cast<M*>(f) = in.get<M>(); };
}

template <class M>  // v3f and col3 — [x,y,z] array
void setVec3(const char* typeName)
{
    auto& slot = fieldTypeSlot<M>();
    slot.typeName = typeName;
    slot.toJson = [](const void* f, nlohmann::json& out, const Engine&)
    {
        const auto& v = *static_cast<const M*>(f);
        out = {v.x(), v.y(), v.z()};
    };
    slot.fromJson = [](void* f, const nlohmann::json& in, const Engine&)
    {
        *static_cast<M*>(f) = M{in[0].get<float>(), in[1].get<float>(), in[2].get<float>()};
    };
}
}  // namespace

void registerBuiltinFieldTypes()
{
    setPlain<float>("float");
    setPlain<bool>("bool");
    setPlain<int32_t>("int32_t");
    setPlain<uint32_t>("uint32_t");
    setPlain<std::string>("std::string");

    setVec3<v3f>("v3f");
    setVec3<col3>("col3");  // same [r,g,b] json as v3f, but gets a color widget

    {
        auto& slot = fieldTypeSlot<quatf>();
        slot.typeName = "quatf";
        // stored [x,y,z,w], Eigen ctor takes (w,x,y,z)
        slot.toJson = [](const void* f, nlohmann::json& out, const Engine&)
        {
            const auto& q = *static_cast<const quatf*>(f);
            out = {q.x(), q.y(), q.z(), q.w()};
        };
        slot.fromJson = [](void* f, const nlohmann::json& in, const Engine&)
        {
            *static_cast<quatf*>(f) = quatf{in[3].get<float>(), in[0].get<float>(),
                                            in[1].get<float>(), in[2].get<float>()};
        };
    }
}

}  // namespace batap
