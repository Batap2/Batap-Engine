#pragma once

#include <assimp/code/AssetLib/Collada/ColladaHelper.h>
#include "Components/ComponentFlag.h"
#include "Components/Transform_C.h"
#include "EigenTypes.h"
#include "Handles.h"

#include "entt/entt.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>

namespace rayvox
{

template <size_t SmallN>
struct TempBytes
{
    std::array<std::byte, SmallN> _small{};
    std::vector<std::byte> _heap{};

    std::byte* get(size_t n)
    {
        if (n <= SmallN)
            return _small.data();
        _heap.resize(n);
        return _heap.data();
    }
};

constexpr size_t flagToIndex(ComponentFlag f)
{
    return static_cast<size_t>(std::countr_zero(static_cast<uint32_t>(f)));
}

struct PatchDesc
{
    uint32_t _offset;
    uint32_t _size;
    void (*fill)(const entt::registry& r, entt::entity e, void* dst);
};

struct PatchRange
{
    std::span<const PatchDesc> patches{};
};

template <class Instance>
struct InstancePatches;

// ----------- Instances : 

struct StaticMeshInstance
{
    static constexpr ComponentFlag UsedComposents = ComponentFlag::Transform;

    uint32_t _gpuIndex;

    struct GPUData // needs to be multiple of 4
    {
        m4f _world;
    };
};

struct CameraInstance{
    static constexpr ComponentFlag UsedComposents = ComponentFlag::Transform | ComponentFlag::Camera;

    
};

// ----------- InstancePatches : How to get components data

template <>
struct InstancePatches<StaticMeshInstance>
{
    static void fillWorld(const entt::registry& r, entt::entity e, void* dst)
    {
        auto* t = r.try_get<Transform_C>(e);
        if (!t)
            return;

        auto* out = reinterpret_cast<m4f*>(dst);
        *out = t->worldMatrix();
    }

    static constexpr PatchDesc _transformPatches[] = {
        PatchDesc{._offset = static_cast<uint32_t>(offsetof(StaticMeshInstance::GPUData, _world)),
                  ._size = static_cast<uint32_t>(sizeof(m4f)),
                  .fill = &fillWorld}};

    static constexpr std::array<PatchRange, 32> byBit = []()
    {
        std::array<PatchRange, 32> t{};
        t[flagToIndex(ComponentFlag::Transform)] = PatchRange{_transformPatches};
        return t;
    }();
};
}  // namespace rayvox
