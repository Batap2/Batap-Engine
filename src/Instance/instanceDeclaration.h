#pragma once

#include <assimp/code/AssetLib/Collada/ColladaHelper.h>
#include "Components/Camera_C.h"
#include "Components/ComponentFlag.h"
#include "Components/Transform_C.h"
#include "Context.h"
#include "EigenTypes.h"
#include "Handles.h"

#include "entt/entt.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>

#pragma clang optimize off

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
    void (*fill)(Context& ctx, const entt::registry& r, entt::entity e, void* dst);
};

struct PatchRange
{
    std::span<const PatchDesc> patches{};
};

template <class Instance>
struct InstancePatches;

// ----------- Instances :

template <class GPUDataT, ComponentFlag UsedFlags>
struct GPUInstanceBase
{
    static constexpr ComponentFlag UsedComposents = UsedFlags;
    uint32_t _gpuIndex = 0;
    using GPUData = GPUDataT;

    static_assert(std::is_trivially_copyable_v<GPUDataT>);
    static_assert((sizeof(GPUDataT) % 4) == 0); 
};

struct StaticMeshGPUData
{
    float _world[16];
};

using StaticMeshInstance = GPUInstanceBase<StaticMeshGPUData, ComponentFlag::Transform>;

struct CameraGPUData
{
    float _view[16];
    float _proj[16];
    float _pos[3];
    float _znear;
    float _right[3];
    float _zfar;
    float _up[3];
    float _fov;
};

using CameraInstance =
    GPUInstanceBase<CameraGPUData, ComponentFlag::Transform | ComponentFlag::Camera>;

// ----------- InstancePatches : How to get components data

template <>
struct InstancePatches<StaticMeshInstance>
{
    static void fillWorld(Context& ctx, const entt::registry& r, entt::entity e, void* dst)
    {
        auto* t = r.try_get<Transform_C>(e);
        if (!t)
            return;

        auto* out = reinterpret_cast<m4f*>(dst);
        *out = t->worldMatrix();
        volatile auto a = out;
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

template <>
struct InstancePatches<CameraInstance>
{
    static void fillCamData(Context& ctx, const entt::registry& r, entt::entity e, void* dst)
    {
        auto* camC = r.try_get<Camera_C>(e);
        if (!camC)
            return;

        auto* transC = r.try_get<Transform_C>(e);

        auto* out = reinterpret_cast<CameraInstance::GPUData*>(dst);
        out->_znear = camC->_znear;
        out->_zfar = camC->_zfar;
        out->_fov = camC->_fov;

        if (transC)
        {
            auto worldM = transC->world();
            auto view = camC->make_view(worldM);
            std::memcpy(out->_view, view.data(), sizeof(out->_view));
            auto frameSize = ctx.getFrameSize();
            auto aspect = static_cast<float>(frameSize.x()) / static_cast<float>(frameSize.y());

            auto proj = camC->make_proj(aspect);
            std::memcpy(out->_proj, proj.data(), sizeof(out->_proj));

            v3f pos = worldM.translation();
            v3f right = worldM.linear().col(0).normalized();
            v3f up = worldM.linear().col(1).normalized();

            std::memcpy(out->_pos, pos.data(), 3 * sizeof(float));
            std::memcpy(out->_right, right.data(), 3 * sizeof(float));
            std::memcpy(out->_up, up.data(), 3 * sizeof(float));
        }
    }

    static constexpr PatchDesc _cameraPatches[] = {
        PatchDesc{._offset = 0,
                  ._size = static_cast<uint32_t>(sizeof(CameraInstance::GPUData)),
                  .fill = &fillCamData}};

    static constexpr std::array<PatchRange, 32> byBit = []()
    {
        std::array<PatchRange, 32> t{};
        t[flagToIndex(ComponentFlag::Camera)] = PatchRange{_cameraPatches};
        t[flagToIndex(ComponentFlag::Transform)] = PatchRange{_cameraPatches};
        return t;
    }();
};
}  // namespace rayvox
