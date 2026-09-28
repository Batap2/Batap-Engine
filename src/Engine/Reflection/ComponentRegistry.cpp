#include "ComponentRegistry.h"

#include "DebugUtils.h"
#include "EigenTypes.h"

#include <nlohmann/json.hpp>

#include <bit>
#include <iostream>
#include <string>
#include <unordered_set>

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

uint32_t ComponentRegistry::claimGPUBit()
{
    static uint32_t next = 0;
    ThrowAssert(next < 64, "more than 64 GPU-read component types — widen ComponentMask");
    return next++;
}

void ComponentRegistry::importFrom(ComponentRegistry& module)
{
    std::unordered_set<std::string_view> provided;

    for (ComponentType& mt : module.types_)
    {
        provided.insert(mt.name);

        ComponentType* existing = nullptr;
        for (ComponentType& t : types_)
            if (t.name == mt.name)
            {
                existing = &t;
                break;
            }

        if (!existing)
        {
            ComponentType copy = mt;
            copy.fromModule_ = true;
            add(std::move(copy));
        }
        else if (existing->fromModule_)
        {
            // Refresh: the old entry's pointers target the unloaded DLL.
            *existing = mt;
            existing->fromModule_ = true;
        }
        else if (mt.bitSlot_ && existing->bitSlot_)
        {
            // Engine type: the module's markDirty<T> must use the host's bit.
            *mt.bitSlot_ = *existing->bitSlot_;
        }
    }

    for (ComponentType& t : types_)
        if (t.fromModule_ && !provided.contains(t.name))
        {
            t.tryGet = nullptr;
            t.getOrEmplace = nullptr;
            t.remove = nullptr;
            t.copy = nullptr;
            t.patch = nullptr;
            t.refusal = nullptr;
            t.bitSlot_ = nullptr;
            t.fields.clear();
        }
}

void logRefusal(std::string_view why)
{
    std::cerr << "[Admission] " << why << '\n';
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

template <class M>
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
    setPlain<uint8_t>("uint8_t");
    setPlain<std::string>("std::string");

    setVec3<v3f>("v3f");
    setVec3<col3>("col3");  // same [r,g,b] json as v3f, but gets a color widget

    {
        auto& slot = fieldTypeSlot<quatf>();
        slot.typeName = "quatf";
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
