#pragma once

// Component reflection registry. Declaring a component once with
// BATAP_COMPONENT gives serialization, deserialization, editor UI and GPU
// dirty routing for free: they are generic loops over the registered field
// lists.
//
//   struct Health_C { float current = 100.f; float max = 100.f; };
//   BATAP_COMPONENT(Health_C, "health");
//
// The json key is spelled out — it is a durable contract with scenes on disk,
// so it must survive a class rename untouched. Each member becomes a json key
// from its own name, with the trailing '_' stripped (color_ -> "color"). GPU
// dirty bits are claimed by the pools that read a component — most components
// never get one.
//
// To change how a field is edited, change its type: `col3` instead of `v3f`
// gives a color picker. Only what nothing else can carry — a slider range, a
// drag speed — is passed as an extra:
//
//   BATAP_COMPONENT(Enemy_C, "enemy",
//       fieldMeta<&Enemy_C::aggro_>({.min = 0.f, .max = 1.f}));
//
// A component feeds the GPU by appearing in an instance's `Uses` list
// (instanceDeclaration.h) — nothing to declare on this side.

#include "Components/EntityHandle.h"
#include "Reflection/ComponentMask.h"
#include "Reflection/StructFields.h"

#include <entt/entt.hpp>
#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace batap
{
struct Engine;
struct World;
struct Field;

struct FieldMeta
{
    float speed = 0.1f;
    float min = 0.f;
    float max = 0.f;
};

// One per C++ field type (float, v3f, MeshHandle, ...). toJson/fromJson are
// filled by registerBuiltinFieldTypes() at engine init; drawUI is installed
// by the editor (stays null in a game-only build and is never called there).
struct FieldType
{
    void (*toJson)(const void* field, nlohmann::json& out, const Engine&) = nullptr;
    void (*fromJson)(void* field, const nlohmann::json& in, const Engine&) = nullptr;
    bool (*drawUI)(void* field, const Field& f) = nullptr;
    const char* typeName = "unregistered";
};

// The mutable slot a given field type lives in. Meyers singleton so game
// code statically registering components never races engine init order.
template <class M>
FieldType& fieldTypeSlot()
{
    static FieldType slot;
    return slot;
}

// An enum borrows its underlying integer's slot — same bytes, same json — so
// enum fields need no registration of their own.
template <class M>
FieldType* fieldTypeFor()
{
    if constexpr (std::is_enum_v<M>)
        return &fieldTypeSlot<std::underlying_type_t<M>>();
    else
        return &fieldTypeSlot<M>();
}

struct Field
{
    std::string name;  // json key / UI label ("castShadows")
    FieldType* type = nullptr;
    size_t offset = 0;
    FieldMeta meta;

    // Type-erased member access is pointer arithmetic by nature.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage"
    void* ptrIn(void* component) const { return static_cast<char*>(component) + offset; }
    const void* ptrIn(const void* component) const
    {
        return static_cast<const char*>(component) + offset;
    }
#pragma clang diagnostic pop
};

struct ComponentMeta
{
    uint32_t version = 1;
    // Post-load hook for components whose state isn't just its fields
    // (e.g. Transform must rebuild matrices through Transform_S).
    void (*onDeserialized)(EntityHandle, World&) = nullptr;
    // The editor draws this component with its own panel (asset pickers and
    // the like), so the generic field loop skips it. Serialization is still
    // fully reflected — this only concerns the inspector.
    bool customEditor = false;
};

struct ComponentType
{
    std::string name;  // json "type" value ("pointLight")
    ComponentMeta meta;
    std::vector<Field> fields;

    // entt ops, type-erased at registration
    void* (*tryGet)(entt::registry&, entt::entity) = nullptr;
    void* (*getOrEmplace)(entt::registry&, entt::entity) = nullptr;
    void (*remove)(entt::registry&, entt::entity) = nullptr;
    void (*copy)(entt::registry&, entt::entity from, entt::entity to) = nullptr;
    // entt's on_update; undefined behaviour on an entity without the component.
    void (*patch)(entt::registry&, entt::entity) = nullptr;

    // The componentBitSlot<T> this type was registered from. importFrom
    // copies the host's bit through it so componentMask<T> agrees across
    // the DLL boundary.
    uint32_t* bitSlot_ = nullptr;

    // Came from a game module: its code pointers must be refreshed on every
    // reload, and nulled if the module stops providing the type. Generic
    // loops must skip an entry whose tryGet is null.
    bool fromModule_ = false;

    ComponentMask mask() const { return bitSlot_ ? maskOfBit(*bitSlot_) : ComponentMask{0}; }
};

struct ComponentRegistry
{
    static ComponentRegistry& instance();

    void add(ComponentType type);
    const ComponentType* find(std::string_view name) const;
    const std::vector<ComponentType>& all() const { return types_; }

    // Merge a game DLL's registry into this one, by name: engine types get
    // the host's bit copied into the module's slot, unknown types are added.
    void importFrom(ComponentRegistry& module);

    // Hard error if any registered field has no serializer — called by the
    // Engine ctor, after builtins are in and static registrations ran.
    void validate() const;

    static uint32_t claimGPUBit();

   private:
    std::vector<ComponentType> types_;
};

template <class C>
ComponentMask claimComponentBit()
{
    uint32_t& slot = componentBitSlot<C>();
    if (slot == InvalidComponentBit)
        slot = ComponentRegistry::claimGPUBit();
    return maskOfBit(slot);
}

// Fills toJson/fromJson for the built-in field types. Called once by the
// Engine ctor.
void registerBuiltinFieldTypes();

// --- per-field override (only exceptions are written) --------------------

struct FieldOverride
{
    size_t offset = 0;
    FieldMeta meta;
};

namespace detail
{
template <class P>
struct MemberPtr;
template <class C, class M>
struct MemberPtr<M C::*>
{
    using Owner = C;
    using Type = M;
};

// offsetof for a member pointer — measured on a probe instance because the
// components are not standard-layout enough for the macro.
template <auto Member>
size_t memberOffset()
{
    using C = typename MemberPtr<decltype(Member)>::Owner;
    C probe{};
    return size_t(reinterpret_cast<const char*>(std::addressof(probe.*Member)) -
                  reinterpret_cast<const char*>(std::addressof(probe)));
}
}  // namespace detail

template <auto Member>
FieldOverride fieldMeta(FieldMeta m)
{
    return {detail::memberOffset<Member>(), m};
}

// Hides a member from the registry: runtime state (a physics body id, a
// cache) that must never reach a .btpl nor the inspector, and would be stale
// anyway after a hot reload.
struct FieldSkip
{
    size_t offset = 0;
};

template <auto Member>
FieldSkip fieldSkip()
{
    return {detail::memberOffset<Member>()};
}

// --- manual registration (non-aggregates) --------------------------------

// One field of a component the aggregate reflection cannot read (Transform_C,
// whose fields are private): the json key is spelled out instead of derived
// from the member name. Instantiate it from inside the owning class when the
// member is private — access is checked where the template argument is
// written.
template <auto Member>
Field field(std::string name, FieldMeta m = {})
{
    using M = typename detail::MemberPtr<decltype(Member)>::Type;
    return Field{std::move(name), fieldTypeFor<M>(), detail::memberOffset<Member>(), m};
}

template <class T>
void addComponentType(std::string_view name, ComponentMeta meta, std::vector<Field> fields)
{
    ComponentType t;
    t.name = name;
    t.meta = meta;
    t.fields = std::move(fields);

    t.tryGet = [](entt::registry& r, entt::entity e) -> void* { return r.try_get<T>(e); };
    t.getOrEmplace = [](entt::registry& r, entt::entity e) -> void*
    { return &r.get_or_emplace<T>(e); };
    t.remove = [](entt::registry& r, entt::entity e) { r.remove<T>(e); };
    t.copy = [](entt::registry& r, entt::entity from, entt::entity to)
    { r.emplace_or_replace<T>(to, r.get<T>(from)); };
    t.patch = [](entt::registry& r, entt::entity e) { r.patch<T>(e); };
    t.bitSlot_ = &componentBitSlot<T>();

    ComponentRegistry::instance().add(std::move(t));
}

// --- registration ------------------------------------------------------------

template <class T, class... Extra>
bool registerComponent(std::string_view name, Extra&&... extra)
{
    ComponentType t;

    // extras come in any order: one optional ComponentMeta + field overrides
    std::vector<FieldOverride> overrides;
    std::vector<FieldSkip> skips;
    auto consume = [&](auto&& item)
    {
        using I = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<I, ComponentMeta>)
            t.meta = item;
        else if constexpr (std::is_same_v<I, FieldSkip>)
            skips.push_back(item);
        else
        {
            static_assert(std::is_same_v<I, FieldOverride>,
                          "BATAP_COMPONENT extras must be ComponentMeta, fieldMeta<...>() or "
                          "fieldSkip<...>()");
            overrides.push_back(item);
        }
    };
    (consume(extra), ...);

    // discover fields from the aggregate
    T probe{};
    auto refs = refl::tieFields(probe);
    [&]<size_t... I>(std::index_sequence<I...>)
    {
        (t.fields.push_back(Field{
             std::string(refl::fieldName<T, I>()),
             fieldTypeFor<refl::FieldTypeAt<I, T>>(),
             size_t(reinterpret_cast<const char*>(std::addressof(std::get<I>(refs))) -
                    reinterpret_cast<const char*>(std::addressof(probe))),
             FieldMeta{}}),
         ...);
    }(std::make_index_sequence<refl::fieldCount<T>()>{});

    for (const auto& o : overrides)
        for (auto& f : t.fields)
            if (f.offset == o.offset)
                f.meta = o.meta;

    for (const auto& s : skips)
        std::erase_if(t.fields, [&](const Field& f) { return f.offset == s.offset; });

    addComponentType<T>(name, t.meta, std::move(t.fields));
    return true;
}

// Registers T at static init. Place after the struct, in its header —
// `inline` collapses the multiple inclusions into one registration.
// Registration at static init is the point: silence -Wglobal-constructors.
#define BATAP_COMPONENT(T, ...)                                              \
    _Pragma("clang diagnostic push")                                         \
    _Pragma("clang diagnostic ignored \"-Wglobal-constructors\"")            \
    inline const bool _batapComponentRegistered_##T =                        \
        ::batap::registerComponent<T>(__VA_ARGS__);                          \
    _Pragma("clang diagnostic pop")

}  // namespace batap
