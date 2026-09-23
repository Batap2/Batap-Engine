#pragma once

// The one file to edit to add something the GPU draws.
//
// An instance is a struct with no base class, giving:
//   GPUData         the ShaderInterop struct one entity occupies in the pool
//   Uses            every component fill() reads; its head is the marker
//                   component whose presence puts an entity in the pool
//   Binding         its frame set slot
//   fill()          how one entity is turned into one GPUData
//
// and optionally InitialCapacity (default 1) and CountField, to push the pool
// size to the shaders. Adding it to GPUInstances at the bottom gives it its
// pool, its upload pass, its dirty routing, its frame set binding and its
// entt hooks.
//
// Uses is checked: fill() sees only the components it lists, so reading one
// that is missing from it is a build error rather than a buffer that silently
// stops updating. The head is guaranteed present — the entity is in the pool
// only while it exists — so in.marker() hands it out by reference; the rest
// may be absent and come back as pointers through in.get<C>().

#include "Assets/AssetManager.h"
#include "Assets/Mesh.h"
#include "Assets/Texture.h"
#include "Components/Camera_C.h"
#include "Components/Materials_C.h"
#include "Components/Mesh_C.h"
#include "Components/PointLight_C.h"
#include "Components/ShadowSphere_C.h"
#include "Components/Skybox_C.h"
#include "Components/Transform_C.h"
#include "Flatten.h"
#include "EigenTypes.h"
#include "Engine.h"
#include "Handles.h"
#include "Reflection/ComponentRegistry.h"
#include "Renderer/SkyIrradiance.h"
#include "Shaders/ShaderInterop.h"

#include "entt/entt.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstring>
#include <span>
#include <type_traits>

namespace batap
{

template <class... Ts>
struct TypeList
{};

// ----------- what an instance sees of an entity ----------------------------

template <class Head, class... Rest>
struct Access
{
    Engine& ctx;
    const entt::registry& reg;
    entt::entity entity;

    // The marker component, by reference: pool membership *is* its presence.
    const Head& marker() const { return reg.get<Head>(entity); }

    template <class C>
    const C* get() const
    {
        static_assert(std::is_same_v<C, Head> || (std::is_same_v<C, Rest> || ...),
                      "this component is missing from the instance's Uses list — add it "
                      "there, or a change to it would never reach the GPU");
        return reg.try_get<C>(entity);
    }
};

namespace detail
{
template <class List>
struct AccessOfList;
template <class... Cs>
struct AccessOfList<TypeList<Cs...>>
{
    using type = Access<Cs...>;
};

template <class List>
struct HeadOfList;
template <class Head, class... Tail>
struct HeadOfList<TypeList<Head, Tail...>>
{
    using type = Head;
};
}  // namespace detail

template <class List>
using AccessOf = typename detail::AccessOfList<List>::type;

// The component that decides pool membership: emplacing it anywhere — factory,
// deserializer, game code — puts the entity in the pool, removing it or the
// entity takes it out.
template <class Instance>
using MarkerOf = typename detail::HeadOfList<typename Instance::Uses>::type;

// ----------- Instances -----------------------------------------------------

struct StaticMeshInstance
{
    using GPUData = StaticMeshGPUData;
    using Uses = TypeList<Mesh_C, Transform_C, Materials_C>;
    static constexpr uint32_t Binding = InstancesBinding;
    static constexpr size_t InitialCapacity = 256;

    static void fill(AccessOf<Uses> in, GPUData& out)
    {
        if (auto* t = in.get<Transform_C>())
            flatten(out.world_, t->worldMatrix());

        auto indices = std::span{out.materialIndices_};
        std::fill(indices.begin(), indices.end(), InvalidGPUIndex);

        auto* mats = in.get<Materials_C>();
        if (!mats)
            return;
        for (size_t i = 0; i < indices.size() && i < mats->slots.size(); ++i)
            if (mats->slots[i])
                indices[i] = mats->slots[i].index;
    }
};

struct CameraInstance
{
    using GPUData = CameraGPUData;
    using Uses = TypeList<Camera_C, Transform_C>;
    static constexpr uint32_t Binding = CamerasBinding;

    static void fill(AccessOf<Uses> in, GPUData& out)
    {
        const Camera_C& cam = in.marker();
        out.znear_ = cam.znear_;
        out.zfar_ = cam.zfar_;
        out.fov_ = cam.fov_;

        auto* trans = in.get<Transform_C>();
        if (!trans)
            return;

        const auto world = trans->world();
        flatten(out.view_, cam.make_view(world));

        const auto frameSize = in.ctx.getFrameSize();
        const auto aspect = static_cast<float>(frameSize.x()) / static_cast<float>(frameSize.y());
        flatten(out.proj_, cam.make_proj(aspect));

        flatten(out.pos_, world.translation());
        flatten(out.right_, world.linear().col(0).normalized());
        flatten(out.up_, world.linear().col(1).normalized());
        flatten(out.fwd_, -world.linear().col(2).normalized());
    }
};

struct PointLightInstance
{
    using GPUData = PointLightGPUData;
    using Uses = TypeList<PointLight_C, Transform_C>;
    static constexpr uint32_t Binding = PointLightsBinding;
    static constexpr size_t InitialCapacity = 32;
    static constexpr uint32_t DrawPush::* CountField = &DrawPush::pointLightCount_;

    static void fill(AccessOf<Uses> in, GPUData& out)
    {
        if (auto* trans = in.get<Transform_C>())
            flatten(out.pos_, trans->world().translation());

        const PointLight_C& light = in.marker();
        flatten(out.color_, light.color_);
        out.intensity_ = light.intensity_;
        out.radius_ = light.radius_;
        out.falloff_ = light.falloff_;
        out.castShadows_ = static_cast<uint32_t>(light.castShadows_);
        out.sourceRadius_ = light.sourceRadius_;
        out.shadowDistance_ = light.shadowDistance_;
    }
};

struct SkyboxInstance
{
    using GPUData = SkyboxGPUData;
    using Uses = TypeList<Skybox_C>;
    static constexpr uint32_t Binding = SkyboxBinding;
    static constexpr uint32_t DrawPush::* CountField = &DrawPush::skyboxCount_;

    static void fill(AccessOf<Uses> in, GPUData& out)
    {
        const Skybox_C& sky = in.marker();

        out.bindlessIndex = InvalidGPUIndex;
        out.mipCount = 1u;

        SH9 sh;
        if (sky.mode_ == Skybox_C::Mode::HDRI && sky.hdri_)
        {
            if (auto* tex = in.ctx.assetManager_->get<Texture>(sky.hdri_))
            {
                sh = tex->irradianceSH_;
                out.bindlessIndex = tex->bindlessIndex_;
                out.mipCount = tex->mipLevels_;
            }
        }
        else
        {
            sh = projectSkyToSH(sky);
        }

        auto outSH = std::span{out.sh};
        for (size_t i = 0; i < outSH.size(); ++i)
            flatten(outSH[i], sh.c[i] * sky.intensity_);

        out.mode = static_cast<uint32_t>(sky.mode_);
        out.intensity = sky.intensity_;
        flatten(out.color1, sky.color1_);
        flatten(out.color2, sky.color2_);
        flatten(out.color3, sky.color3_);
        out.horizonWidth = sky.horizonWidth_;
    }
};

struct ShadowSphereInstance
{
    using GPUData = SphereOccluderGPUData;
    using Uses = TypeList<ShadowSphere_C, Mesh_C, Transform_C>;
    static constexpr uint32_t Binding = SphereOccludersBinding;
    static constexpr size_t InitialCapacity = 8;
    static constexpr uint32_t DrawPush::* CountField = &DrawPush::sphereOccluderCount_;

    // Rides on an entity that already has an aspect instead of defining one,
    // so the inspector treats it as an ordinary component. Nothing else needs
    // changing: hooks are per marker and markDirty tests pool.contains() pool
    // by pool, so two memberships already coexist.
    static constexpr bool Additive = true;

    static void fill(AccessOf<Uses> in, GPUData& out)
    {
        // Inert until proven otherwise: the shader skips a zero radius.
        out.radius_ = 0.f;

        auto* trans = in.get<Transform_C>();
        if (!trans)
            return;

        const auto world = trans->world();
        flatten(out.center_, world.translation());

        if (const float radiusOverride = in.marker().radius_; radiusOverride > 0.f)
        {
            out.radius_ = radiusOverride;
            return;
        }

        auto* meshC = in.get<Mesh_C>();
        if (!meshC || !meshC->mesh_)
            return;

        const Mesh* mesh = in.ctx.assetManager_->get(meshC->mesh_);
        if (!mesh || !mesh->localBounds_.valid())
            return;

        const AABB bounds = mesh->localBounds_.transformed(world);
        flatten(out.center_, bounds.center());

        out.radius_ = bounds.halfSize().maxCoeff();
    }
};

// ----------- GPUInstances : the one list the plumbing reads -----------------

using GPUInstances = TypeList<StaticMeshInstance, CameraInstance, PointLightInstance,
                              SkyboxInstance, ShadowSphereInstance>;

// What the plumbing assumes of an instance, checked where it is declared
// rather than deep in a pool instantiation.
template <class T>
concept GPUInstance = requires {
    typename T::GPUData;
    typename T::Uses;
    { T::Binding } -> std::convertible_to<uint32_t>;
    requires std::is_trivially_copyable_v<typename T::GPUData>;
    requires(sizeof(typename T::GPUData) % 4) == 0;
};

template <class Instance>
constexpr size_t initialCapacityOf()
{
    if constexpr (requires { Instance::InitialCapacity; })
        return Instance::InitialCapacity;
    else
        return 1;
}

// The components whose change must re-upload this instance. Building this
// mask at pool construction is what CLAIMS the GPU bits: only components
// named in a Uses list ever get one.
namespace detail
{
template <class... Cs>
ComponentMask maskOfList(TypeList<Cs...>*)
{
    return (ComponentMask{0} | ... | claimComponentBit<Cs>());
}
}  // namespace detail

template <class Instance>
ComponentMask usedComponentMask()
{
    return detail::maskOfList(static_cast<typename Instance::Uses*>(nullptr));
}

// An instance may declare Additive: its marker adds a pool to an entity that
// already has an aspect, instead of being that aspect. ShadowSphere_C is one —
// it rides on a mesh. The pools were always independent (hooks per marker,
// markDirty testing pool.contains one by one); what Additive changes is only
// whether the UI reads the marker as the entity's kind.
template <class Instance>
constexpr bool isAdditiveInstance()
{
    if constexpr (requires { Instance::Additive; })
        return Instance::Additive;
    else
        return false;
}

// The components whose presence defines an entity's rendering *aspect* — at
// most one per entity, chosen at spawn (Spawnable.h) and not amended after, so
// a UI offering components to add must exclude these. Additive markers are not
// aspects and stay out of the mask: they are added and removed like any other
// component.
namespace detail
{
template <class... Is>
ComponentMask markerMaskOfList(TypeList<Is...>*)
{
    return (ComponentMask{0} | ... |
            (isAdditiveInstance<Is>() ? ComponentMask{0} : componentMask<MarkerOf<Is>>()));
}
}  // namespace detail

inline ComponentMask markerComponentMask()
{
    return detail::markerMaskOfList(static_cast<GPUInstances*>(nullptr));
}

// Bindings the pools claim. The other half of the check — that every binding
// ends up written by someone, pool or not — lives in ScenePasses::writeFrameSet,
// the only place that knows the non-pool writers.
template <class InstanceList>
struct FrameSetBindings;

template <class... Instances>
struct FrameSetBindings<TypeList<Instances...>>
{
    static_assert((GPUInstance<Instances> && ...));
    static_assert(((Instances::Binding < FrameSetBindingCount) && ...),
                  "an instance claims a binding outside the frame set");

    static constexpr uint32_t claimed = ((1u << Instances::Binding) | ...);

    static_assert(std::popcount(claimed) == sizeof...(Instances),
                  "two instances claim the same frame set binding");
};

inline constexpr FrameSetBindings<GPUInstances> frameSetBindings_{};

}  // namespace batap
