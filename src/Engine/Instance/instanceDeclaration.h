#pragma once

#include "Assets/AssetManager.h"
#include "Assets/Mesh.h"
#include "Assets/Texture.h"
#include "Components/Camera_C.h"
#include "Components/ComponentFlag.h"
#include "Components/Materials_C.h"
#include "Components/Mesh_C.h"
#include "Components/PointLight_C.h"
#include "Components/Skybox_C.h"
#include "Components/Transform_C.h"
#include "Engine.h"
#include "EigenTypes.h"
#include "Handles.h"
#include "Instance/EntityKind.h"
#include "Renderer/SkyIrradiance.h"

#include "entt/entt.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>

namespace batap
{

template <class... Ts>
struct TypeList
{
};

template <size_t SmallN>
struct TempBytes
{
    std::array<std::byte, SmallN> _small{};
    std::vector<std::byte> _heap{};

    std::span<std::byte> get(size_t n)
    {
        if (n <= SmallN)
            return std::span<std::byte>(_small).first(n);

        _heap.resize(n);
        return std::span<std::byte>(_heap);
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
    void (*fill)(Engine& ctx, const entt::registry& r, entt::entity e, void* dst);
};

struct PatchRange
{
    std::span<const PatchDesc> patches{};
};

template <class Instance>
struct InstancePatches;

// ----------- Instances :

// Everything the engine needs to know about one GPU-mirrored aspect: its
// buffer layout, which components invalidate it, and how to size and name its
// pool. Deriving instead of aliasing is what lets a type add InitialCapacity
// and PoolName, so a pool is fully described where its instance is declared —
// GPUInstanceManager just holds one per declared kind.
template <class GPUDataT, ComponentFlag UsedFlags>
struct GPUInstanceBase
{
    static constexpr ComponentFlag UsedComposents = UsedFlags;
    using GPUData = GPUDataT;

    // Overridable by the derived instance type.
    static constexpr size_t InitialCapacity = 1;
    static constexpr const char* PoolName = "FrameInstancePool";

    static_assert(std::is_trivially_copyable_v<GPUDataT>);
    static_assert((sizeof(GPUDataT) % 4) == 0);
};

struct StaticMeshGPUData
{
    float _world[16];              // 64 bytes — world matrix
    uint32_t _materialIndices[8];  // 32 bytes — GPU arena slot per submesh (0xFFFFFFFF = none)
};
static_assert(sizeof(StaticMeshGPUData) == 96);

struct StaticMeshInstance
    : GPUInstanceBase<StaticMeshGPUData,
                      ComponentFlag::Mesh | ComponentFlag::Transform | ComponentFlag::Materials>
{
    static constexpr size_t InitialCapacity = 256;
    static constexpr const char* PoolName = "StaticMeshInstancePool";
};

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
    float _fwd[3];
    float _pad;
};

struct CameraInstance
    : GPUInstanceBase<CameraGPUData, ComponentFlag::Transform | ComponentFlag::Camera>
{
    static constexpr size_t InitialCapacity = 1;
    static constexpr const char* PoolName = "CameraInstancePool";
};

struct PointLightGPUData
{
    float pos_[3];
    float intensity_;
    float color_[3];
    float radius_;
    float falloff_;
    uint32_t castShadows_;
};

struct PointLightInstance
    : GPUInstanceBase<PointLightGPUData, ComponentFlag::Transform | ComponentFlag::PointLight>
{
    static constexpr size_t InitialCapacity = 32;
    static constexpr const char* PoolName = "pointLightInstancePool";
};

// ----------- InstancePatches : How to get components data

template <>
struct InstancePatches<StaticMeshInstance>
{
    static void fillWorld(Engine& ctx, const entt::registry& r, entt::entity e, void* dst)
    {
        auto* t = r.try_get<Transform_C>(e);
        if (!t)
            return;
        std::memcpy(dst, t->worldMatrix().data(), 16 * sizeof(float));
    }

    static void fillMaterials(Engine& ctx, const entt::registry& r, entt::entity e, void* dst)
    {
        auto* out = reinterpret_cast<StaticMeshInstance::GPUData*>(dst);
        std::fill(std::begin(out->_materialIndices), std::end(out->_materialIndices), 0xFFFFFFFFu);

        auto* matsC = r.try_get<Materials_C>(e);
        if (!matsC)
            return;
        auto idxSpan = std::span{out->_materialIndices};
        for (uint8_t i = 0; i < matsC->count && i < 8; ++i)
            if (matsC->slots[i])
                idxSpan[i] = matsC->slots[i].index;
    }

    static constexpr PatchDesc _transformPatch[] = {
        PatchDesc{._offset = offsetof(StaticMeshGPUData, _world),
                  ._size = 16 * sizeof(float),
                  .fill = &fillWorld}};

    static constexpr PatchDesc _materialsPatch[] = {
        PatchDesc{._offset = offsetof(StaticMeshGPUData, _materialIndices),
                  ._size = 8 * sizeof(uint32_t),
                  .fill = &fillMaterials}};

    static constexpr std::array<PatchRange, 32> byBit = []()
    {
        std::array<PatchRange, 32> t{};
        t[flagToIndex(ComponentFlag::Transform)] = PatchRange{_transformPatch};
        // ComponentFlag::Mesh : plus de miroir GPU (un dirty Mesh est un no-op)
        t[flagToIndex(ComponentFlag::Materials)] = PatchRange{_materialsPatch};
        return t;
    }();
};

template <>
struct InstancePatches<CameraInstance>
{
    static void fillCamData(Engine& ctx, const entt::registry& r, entt::entity e, void* dst)
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

            v3f pos   = worldM.translation();
            v3f right = worldM.linear().col(0).normalized();
            v3f up    = worldM.linear().col(1).normalized();
            v3f fwd   = -worldM.linear().col(2).normalized();

            std::memcpy(out->_pos,   pos.data(),   3 * sizeof(float));
            std::memcpy(out->_right, right.data(), 3 * sizeof(float));
            std::memcpy(out->_up,    up.data(),    3 * sizeof(float));
            std::memcpy(out->_fwd,   fwd.data(),   3 * sizeof(float));
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

template <>
struct InstancePatches<PointLightInstance>
{
    static void fillTransform(Engine& ctx, const entt::registry& r, entt::entity e, void* dst)
    {
        auto* transC = r.try_get<Transform_C>(e);
        auto* out = reinterpret_cast<PointLightInstance::GPUData*>(dst);

        if (transC)
        {
            v3f worldPos = transC->world().translation();
            std::memcpy(out->pos_, worldPos.data(), 3 * sizeof(float));
        }
    }

    static void fillLight(Engine& ctx, const entt::registry& r, entt::entity e, void* dst)
    {
        auto* pLightC = r.try_get<PointLight_C>(e);

        auto* out = reinterpret_cast<PointLightInstance::GPUData*>(dst);
        if (pLightC)
        {
            std::memcpy(out->color_, pLightC->color_.data(), 3 * sizeof(float));
            out->intensity_ = pLightC->intensity_;
            out->radius_ = pLightC->radius_;
            out->falloff_ = pLightC->falloff_;
            out->castShadows_ = static_cast<uint32_t>(pLightC->castShadows_);
        }
    }

    static constexpr PatchDesc posPatch_[] = {PatchDesc{
        ._offset = 0, ._size = static_cast<uint32_t>(3 * sizeof(float)), .fill = &fillTransform}};

    static constexpr PatchDesc lightPatch_[] = {
        PatchDesc{._offset = static_cast<uint32_t>(3 * sizeof(float)),
                  ._size = static_cast<uint32_t>(7 * sizeof(float)),
                  .fill = &fillLight}};

    static constexpr std::array<PatchRange, 32> byBit = []()
    {
        std::array<PatchRange, 32> t{};
        t[flagToIndex(ComponentFlag::PointLight)] = PatchRange{lightPatch_};
        t[flagToIndex(ComponentFlag::Transform)] = PatchRange{posPatch_};
        return t;
    }();
};

// ---- Skybox -----------------------------------------------------------------

struct SkyboxGPUData
{
    std::array<std::array<float, 4>, 9> sh;  // SH L2 irradiance — 9 × float4 = 144 bytes
    uint32_t                 mode;           // 0=HDRI, 1=FlatColor, 2=Gradient
    uint32_t                 heapIdx;        // bindless HDRI index (0xFFFFFFFF si absent)
    uint32_t                 mipCount;       // nb mips de la texture HDRI
    float                    intensity;      // multiplicateur spéculaire
    std::array<float, 4>     color1;         // zenith  (xyz=RGB, w=0)
    std::array<float, 4>     color2;         // horizon
    std::array<float, 4>     color3;         // nadir
    float                    horizonWidth;
    std::array<float, 3>     pad;
    // Total : 144 + 16 + 48 + 16 = 224 bytes
};

struct SkyboxInstance : GPUInstanceBase<SkyboxGPUData, ComponentFlag::Skybox>
{
    static constexpr size_t InitialCapacity = 1;
    static constexpr const char* PoolName = "SkyboxInstancePool";
};

template <>
struct InstancePatches<SkyboxInstance>
{
    static void fillSkyboxData(Engine& ctx, const entt::registry& r, entt::entity e, void* dst)
    {
        auto* sky = r.try_get<Skybox_C>(e);
        if (!sky)
            return;
        auto* out = reinterpret_cast<SkyboxGPUData*>(dst);

        SH9 sh;
        if (sky->mode_ == Skybox_C::Mode::HDRI && sky->hdri_)
        {
            if (auto* tex = ctx._assetManager->get<Texture>(sky->hdri_))
                sh = tex->irradianceSH_;
        }
        else
        {
            sh = projectSkyToSH(*sky);
        }

        for (size_t i = 0; i < 9; ++i)
        {
            out->sh[i][0] = sh.c[i].x() * sky->intensity_;
            out->sh[i][1] = sh.c[i].y() * sky->intensity_;
            out->sh[i][2] = sh.c[i].z() * sky->intensity_;
            out->sh[i][3] = 0.0f;
        }

        out->mode         = static_cast<uint32_t>(sky->mode_);
        out->intensity    = sky->intensity_;
        out->color1       = {sky->color1_.x(), sky->color1_.y(), sky->color1_.z(), 0.0f};
        out->color2       = {sky->color2_.x(), sky->color2_.y(), sky->color2_.z(), 0.0f};
        out->color3       = {sky->color3_.x(), sky->color3_.y(), sky->color3_.z(), 0.0f};
        out->horizonWidth = sky->horizonWidth_;
        out->heapIdx      = 0xFFFFFFFFu;
        out->mipCount     = 1u;
        out->pad          = {};
        if (sky->mode_ == Skybox_C::Mode::HDRI && sky->hdri_)
            if (auto* tex = ctx._assetManager->get<Texture>(sky->hdri_))
            {
                out->heapIdx  = tex->heapIdx_;
                out->mipCount = tex->mipLevels_;
            }
    }

    static constexpr PatchDesc _patch[] = {
        {0, static_cast<uint32_t>(sizeof(SkyboxGPUData)), &fillSkyboxData}};

    static constexpr std::array<PatchRange, 32> byBit = []()
    {
        std::array<PatchRange, 32> t{};
        t[flagToIndex(ComponentFlag::Skybox)] = PatchRange{_patch};
        return t;
    }();
};

// ----------- GPUKinds : which entity kinds own a GPU instance ---------------

// The one list the plumbing reads. Adding a line here gives the new kind its
// pool, its upload pass, its dirty routing and its teardown — GPUInstanceManager
// and EntityFactory iterate this, they never name a pool.
//
// A kind absent from the list (EntityKind::Empty) simply has no GPU mirror;
// every visit over it is a no-op rather than a case to remember.
template <EntityKind K, class Instance>
struct GPUKind
{
    static constexpr EntityKind kind = K;
    using InstanceType = Instance;
};

using GPUKinds = TypeList<GPUKind<EntityKind::StaticMesh, StaticMeshInstance>,
                          GPUKind<EntityKind::Camera, CameraInstance>,
                          GPUKind<EntityKind::PointLight, PointLightInstance>,
                          GPUKind<EntityKind::Skybox, SkyboxInstance>>;

}  // namespace batap
