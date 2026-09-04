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
#include "Shaders/ShaderInterop.h"

#include "entt/entt.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <span>

namespace batap
{

template <class... Ts>
struct TypeList
{
};

inline void storeRGB(float4& dst, const v3f& rgb)
{
    dst[0] = rgb.x();
    dst[1] = rgb.y();
    dst[2] = rgb.z();
    dst[3] = 0.0f;
}

template <class Instance>
struct InstanceFill;

// ----------- Instances :

// Derived rather than aliased so an instance can override InitialCapacity and
// PoolName.
template <class GPUDataT, ComponentFlag UsedFlags>
struct GPUInstanceBase
{
    static constexpr ComponentFlag UsedComposents = UsedFlags;
    using GPUData = GPUDataT;

    static constexpr size_t InitialCapacity = 1;
    static constexpr const char* PoolName = "FrameInstancePool";

    static_assert(std::is_trivially_copyable_v<GPUDataT>);
    static_assert((sizeof(GPUDataT) % 4) == 0);
};

struct StaticMeshInstance
    : GPUInstanceBase<StaticMeshGPUData,
                      ComponentFlag::Mesh | ComponentFlag::Transform | ComponentFlag::Materials>
{
    static constexpr size_t InitialCapacity = 256;
    static constexpr const char* PoolName = "StaticMeshInstancePool";
};

struct CameraInstance
    : GPUInstanceBase<CameraGPUData, ComponentFlag::Transform | ComponentFlag::Camera>
{
    static constexpr size_t InitialCapacity = 1;
    static constexpr const char* PoolName = "CameraInstancePool";
};

struct PointLightInstance
    : GPUInstanceBase<PointLightGPUData, ComponentFlag::Transform | ComponentFlag::PointLight>
{
    static constexpr size_t InitialCapacity = 32;
    static constexpr const char* PoolName = "pointLightInstancePool";
};

struct SkyboxInstance : GPUInstanceBase<SkyboxGPUData, ComponentFlag::Skybox>
{
    static constexpr size_t InitialCapacity = 1;
    static constexpr const char* PoolName = "SkyboxInstancePool";
};

// ----------- InstanceFill : how one instance is built from its components

// Runs whenever any of the instance's UsedComposents changed, and rewrites the
// whole struct. `out` arrives zero-initialised.

template <>
struct InstanceFill<StaticMeshInstance>
{
    static void fill(Engine& ctx, const entt::registry& r, entt::entity e, StaticMeshGPUData& out)
    {
        if (auto* t = r.try_get<Transform_C>(e))
            std::memcpy(out.world_, t->worldMatrix().data(), sizeof(out.world_));

        auto idxSpan = std::span{out.materialIndices_};
        std::fill(idxSpan.begin(), idxSpan.end(), InvalidGPUIndex);

        auto* matsC = r.try_get<Materials_C>(e);
        if (!matsC)
            return;
        for (uint8_t i = 0; i < matsC->count && i < 8; ++i)
            if (matsC->slots[i])
                idxSpan[i] = matsC->slots[i].index;
    }
};

template <>
struct InstanceFill<CameraInstance>
{
    static void fill(Engine& ctx, const entt::registry& r, entt::entity e, CameraGPUData& out)
    {
        auto* camC = r.try_get<Camera_C>(e);
        if (!camC)
            return;

        out.znear_ = camC->znear_;
        out.zfar_ = camC->zfar_;
        out.fov_ = camC->fov_;

        auto* transC = r.try_get<Transform_C>(e);
        if (!transC)
            return;

        auto worldM = transC->world();
        auto view = camC->make_view(worldM);
        std::memcpy(out.view_, view.data(), sizeof(out.view_));

        auto frameSize = ctx.getFrameSize();
        auto aspect = static_cast<float>(frameSize.x()) / static_cast<float>(frameSize.y());
        auto proj = camC->make_proj(aspect);
        std::memcpy(out.proj_, proj.data(), sizeof(out.proj_));

        v3f pos   = worldM.translation();
        v3f right = worldM.linear().col(0).normalized();
        v3f up    = worldM.linear().col(1).normalized();
        v3f fwd   = -worldM.linear().col(2).normalized();

        std::memcpy(out.pos_,   pos.data(),   sizeof(out.pos_));
        std::memcpy(out.right_, right.data(), sizeof(out.right_));
        std::memcpy(out.up_,    up.data(),    sizeof(out.up_));
        std::memcpy(out.fwd_,   fwd.data(),   sizeof(out.fwd_));
    }
};

template <>
struct InstanceFill<PointLightInstance>
{
    static void fill(Engine& ctx, const entt::registry& r, entt::entity e, PointLightGPUData& out)
    {
        if (auto* transC = r.try_get<Transform_C>(e))
        {
            v3f worldPos = transC->world().translation();
            std::memcpy(out.pos_, worldPos.data(), sizeof(out.pos_));
        }

        if (auto* pLightC = r.try_get<PointLight_C>(e))
        {
            std::memcpy(out.color_, pLightC->color_.data(), sizeof(out.color_));
            out.intensity_ = pLightC->intensity_;
            out.radius_ = pLightC->radius_;
            out.falloff_ = pLightC->falloff_;
            out.castShadows_ = static_cast<uint32_t>(pLightC->castShadows_);
        }
    }
};

template <>
struct InstanceFill<SkyboxInstance>
{
    static void fill(Engine& ctx, const entt::registry& r, entt::entity e, SkyboxGPUData& out)
    {
        auto* sky = r.try_get<Skybox_C>(e);
        if (!sky)
            return;

        out.bindlessIndex = InvalidGPUIndex;
        out.mipCount = 1u;

        SH9 sh;
        if (sky->mode_ == Skybox_C::Mode::HDRI && sky->hdri_)
        {
            if (auto* tex = ctx.assetManager_->get<Texture>(sky->hdri_))
            {
                sh = tex->irradianceSH_;
                out.bindlessIndex = tex->bindlessIndex_;
                out.mipCount = tex->mipLevels_;
            }
        }
        else
        {
            sh = projectSkyToSH(*sky);
        }

        for (size_t i = 0; i < 9; ++i)
        {
            out.sh[i][0] = sh.c[i].x() * sky->intensity_;
            out.sh[i][1] = sh.c[i].y() * sky->intensity_;
            out.sh[i][2] = sh.c[i].z() * sky->intensity_;
            out.sh[i][3] = 0.0f;
        }

        out.mode         = static_cast<uint32_t>(sky->mode_);
        out.intensity    = sky->intensity_;
        storeRGB(out.color1, sky->color1_);
        storeRGB(out.color2, sky->color2_);
        storeRGB(out.color3, sky->color3_);
        out.horizonWidth = sky->horizonWidth_;
    }
};

// ----------- GPUKinds : which entity kinds own a GPU instance ---------------

// The one list the plumbing reads: a kind added here gets its pool, its upload
// pass, its dirty routing and its teardown. A kind absent from it has no GPU
// mirror, and every visit over it is a no-op.
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
