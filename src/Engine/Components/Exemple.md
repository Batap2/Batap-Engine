# Adding a component

## CPU-only — one file, plus one include line

The header is included by `Reflection/ComponentRegistry.cpp` (engine) or the
game's `GameComponents.h`: only there does `BATAP_COMPONENT` register.

```cpp
// Components/Health_C.h
struct Health_C
{
    float current_ = 100.f;
    float max_ = 100.f;
};
BATAP_COMPONENT(Health_C, "health");  // json key = durable contract, never rename
```

Serialization, inspector and add-component menu are derived from the fields.
Must be an aggregate; field types must be registered (see
`registerBuiltinFieldTypes`).

## Meta (optional extras of BATAP_COMPONENT, any order)

```cpp
BATAP_COMPONENT(Health_C, "health",
    ComponentMeta{
        .version = 2,                        // written in scene files, for future migrations
        .onDeserialized = &rebuildAfterLoad, // post-load hook (state not derivable from fields)
        .customEditor = true,                // generic inspector skipped, panel drawn by hand
    },
    fieldMeta<&Health_C::max_>({.speed = 1.f, .min = 0.f, .max = 1000.f}));  // UI drag/range
```

To change how a field is *edited*, prefer changing its type (`col3` = color
picker); `fieldMeta` only carries what a type can't (range, drag speed).

## GPU-visible — two files more

`Shaders/ShaderInterop.h`: the GPU struct + a `FrameSetBinding` slot.

`Instance/InstanceDeclaration.h`: one block, one line in `GPUInstances`.

```cpp
struct HealthInstance
{
    using GPUData = HealthGPUData;
    using Uses = TypeList<Health_C, Transform_C>;  // head = marker: its presence = pool membership
    static constexpr uint32_t Binding = HealthBinding;

    static void fill(AccessOf<Uses> in, GPUData& out)
    {
        out.ratio_ = in.marker().current_ / in.marker().max_;  // marker: always present
        if (auto* t = in.get<Transform_C>())                   // rest: may be absent
            store(out.pos_, t->world().translation());
    }
};

using GPUInstances = TypeList<..., HealthInstance>;
```

Pool, upload, entt hooks, dirty routing and binding checks all follow.
Writes must go through `EntityHandle::write<T>` (or `markDirty`) to reach the GPU.

In the shader:

```hlsl
[[vk::binding(HealthBinding, FrameSet)]]
StructuredBuffer<HealthGPUData> HealthBuffer;
```

## Spawnable in the editor (optional)

```cpp
// Instance/Spawnable.h
spawnable<Health_C, Transform_C>("health", "Health", ICON_MD_FAVORITE),
```
