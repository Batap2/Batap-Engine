#include "ComponentSerializers.h"
#include "EntityDesc.h"

#include "Assets/AssetLoader.h"
#include "Assets/AssetManager.h"
#include "Assets/Material.h"
#include "Assets/Mesh.h"
#include "Assets/Texture.h"
#include "Components/Camera_C.h"
#include "Components/Materials_C.h"
#include "Components/Mesh_C.h"
#include "Components/Skybox_C.h"
#include "Components/Transform_C.h"
#include "Engine.h"
#include "Systems/Systems.h"
#include "Systems/Transform_S.h"
#include "World.h"

#include <array>
#include <entt/entt.hpp>

namespace batap
{

// --- helpers -----------------------------------------------------------------

static nlohmann::json toJson(const v3f& v)
{
    return {v.x(), v.y(), v.z()};
}
static nlohmann::json toJson(const quatf& q)
{
    return {q.x(), q.y(), q.z(), q.w()};
}

static v3f fromJsonV3f(const nlohmann::json& j)
{
    return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>()};
}

static quatf fromJsonQuat(const nlohmann::json& j)
{
    // stored [x,y,z,w], Eigen ctor (w,x,y,z)
    return {j[3].get<float>(), j[0].get<float>(), j[1].get<float>(), j[2].get<float>()};
}

// --- Transform ---------------------------------------------------------------

nlohmann::json toJson(const Transform_C& c)
{
    return {{"pos", toJson(c.pos())}, {"rot", toJson(c.rot())}, {"scale", toJson(c.scale())}};
}

static std::optional<ComponentDesc> extractTransform(EntityHandle h, const Engine&)
{
    auto* tc = h._reg->try_get<Transform_C>(h._entity);
    if (!tc)
        return std::nullopt;
    return *tc;
}

static std::optional<nlohmann::json> serializeTransform(EntityHandle h, const Engine& ctx)
{
    if (auto d = extractTransform(h, ctx))
        return toJson(std::get<Transform_C>(*d));
    return std::nullopt;
}

static void deserializeTransform(EntityHandle h, const nlohmann::json& j, uint32_t /*version*/,
                                 const Engine&, World& world)
{
    auto _ = h._reg->get_or_emplace<Transform_C>(h._entity);
    auto& ts = *world.systems_->_transforms;
    ts.setLocalPosition(h, fromJsonV3f(j["pos"]));
    ts.setLocalRotation(h, fromJsonQuat(j["rot"]));
    ts.setLocalScale(h, fromJsonV3f(j["scale"]));
}

// --- Mesh --------------------------------------------------------------------

nlohmann::json toJson(const MeshDesc& d)
{
    return {{"path", d.path.empty() ? nlohmann::json(nullptr) : nlohmann::json(d.path)}};
}

static std::optional<ComponentDesc> extractMesh(EntityHandle h, const Engine& ctx)
{
    auto* mc = h._reg->try_get<Mesh_C>(h._entity);
    if (!mc)
        return std::nullopt;
    MeshDesc desc;
    if (mc->_mesh)
        if (auto* p = ctx._assetManager->getPath<Mesh>(mc->_mesh))
            desc.path = *p;
    return desc;
}

static std::optional<nlohmann::json> serializeMesh(EntityHandle h, const Engine& ctx)
{
    if (auto d = extractMesh(h, ctx))
        return toJson(std::get<MeshDesc>(*d));
    return std::nullopt;
}

static void deserializeMesh(EntityHandle h, const nlohmann::json& j, uint32_t /*version*/,
                            const Engine& ctx, World&)
{
    auto* mc = h._reg->try_get<Mesh_C>(h._entity);
    if (!mc)
        return;
    if (j.contains("path") && !j["path"].is_null())
    {
        const std::string path = j["path"].get<std::string>();
        auto handle = ctx._assetManager->getHandle<Mesh>(path);
        if (!handle)
            if (auto any = loadAsset(path, ctx))
                handle = std::get<AssetHandle<Mesh>>(*any);
        if (handle)
            mc->_mesh = *handle;
    }
}

// --- Materials ---------------------------------------------------------------

nlohmann::json toJson(const MaterialsDesc& d)
{
    nlohmann::json arr = nlohmann::json::array();
    for (uint8_t i = 0; i < d.count; ++i)
        arr.push_back(d.paths[i].empty() ? nlohmann::json(nullptr) : nlohmann::json(d.paths[i]));
    return {{"materials", arr}};
}

static std::optional<ComponentDesc> extractMaterials(EntityHandle h, const Engine& ctx)
{
    auto* mc = h._reg->try_get<Materials_C>(h._entity);
    if (!mc || mc->count == 0)
        return std::nullopt;
    MaterialsDesc desc;
    desc.count = mc->count;
    for (uint8_t i = 0; i < mc->count; ++i)
        if (auto* p = ctx._assetManager->getPath<Material>(mc->slots[i]))
            desc.paths[i] = *p;
    return desc;
}

static std::optional<nlohmann::json> serializeMaterials(EntityHandle h, const Engine& ctx)
{
    if (auto d = extractMaterials(h, ctx))
        return toJson(std::get<MaterialsDesc>(*d));
    return std::nullopt;
}

static void deserializeMaterials(EntityHandle h, const nlohmann::json& j, uint32_t /*version*/,
                                 const Engine& ctx, World&)
{
    if (!j.contains("materials") || !j["materials"].is_array())
        return;
    auto& mc = h._reg->get_or_emplace<Materials_C>(h._entity);
    mc.count = 0;
    for (const auto& entry : j["materials"])
    {
        if (mc.count >= 8)
            break;
        if (entry.is_null())
        {
            ++mc.count;
            continue;
        }
        const std::string path = entry.get<std::string>();
        auto handle = ctx._assetManager->getHandle<Material>(path);
        if (!handle)
            if (auto any = loadAsset(path, ctx))
                handle = std::get<MaterialHandle>(*any);
        if (handle)
            mc.slots[mc.count] = *handle;
        ++mc.count;
    }
}

// --- Camera ------------------------------------------------------------------

nlohmann::json toJson(const Camera_C& c)
{
    return {{"znear", c._znear}, {"zfar", c._zfar}, {"fov", c._fov}, {"active", c._active}};
}

static std::optional<ComponentDesc> extractCamera(EntityHandle h, const Engine&)
{
    auto* cam = h._reg->try_get<Camera_C>(h._entity);
    if (!cam)
        return std::nullopt;
    return *cam;
}

static std::optional<nlohmann::json> serializeCamera(EntityHandle h, const Engine& ctx)
{
    if (auto d = extractCamera(h, ctx))
        return toJson(std::get<Camera_C>(*d));
    return std::nullopt;
}

static void deserializeCamera(EntityHandle h, const nlohmann::json& j, uint32_t /*version*/,
                              const Engine&, World&)
{
    auto* cam = h._reg->try_get<Camera_C>(h._entity);
    if (!cam)
        return;
    cam->_znear = j["znear"].get<float>();
    cam->_zfar = j["zfar"].get<float>();
    cam->_fov = j["fov"].get<float>();
    cam->_active = j["active"].get<bool>();
}

// --- Skybox ------------------------------------------------------------------

nlohmann::json toJson(const SkyboxDesc& d)
{
    return {
        {"hdri", d.hdriPath.empty() ? nlohmann::json(nullptr) : nlohmann::json(d.hdriPath)},
        {"mode", d.mode},
        {"color1", toJson(d.color1)},
        {"color2", toJson(d.color2)},
        {"color3", toJson(d.color3)},
        {"horizonWidth", d.horizonWidth},
        {"intensity", d.intensity},
    };
}

static std::optional<ComponentDesc> extractSkybox(EntityHandle h, const Engine& ctx)
{
    auto* sky = h._reg->try_get<Skybox_C>(h._entity);
    if (!sky)
        return std::nullopt;
    SkyboxDesc desc;
    desc.mode = static_cast<uint32_t>(sky->mode_);
    desc.color1 = sky->color1_;
    desc.color2 = sky->color2_;
    desc.color3 = sky->color3_;
    desc.horizonWidth = sky->horizonWidth_;
    desc.intensity    = sky->intensity_;
    if (sky->hdri_)
        if (auto* p = ctx._assetManager->getPath<Texture>(sky->hdri_))
            desc.hdriPath = *p;
    return desc;
}

static std::optional<nlohmann::json> serializeSkybox(EntityHandle h, const Engine& ctx)
{
    if (auto d = extractSkybox(h, ctx))
        return toJson(std::get<SkyboxDesc>(*d));
    return std::nullopt;
}

static void deserializeSkybox(EntityHandle h, const nlohmann::json& j, uint32_t /*version*/,
                              const Engine& ctx, World&)
{
    auto* sky = h._reg->try_get<Skybox_C>(h._entity);
    if (!sky)
        return;
    sky->mode_ = static_cast<Skybox_C::Mode>(j.value("mode", 0u));
    sky->horizonWidth_ = j.value("horizonWidth", 0.15f);
    sky->intensity_    = j.value("intensity", 1.0f);
    if (j.contains("color1"))
        sky->color1_ = fromJsonV3f(j["color1"]);
    if (j.contains("color2"))
        sky->color2_ = fromJsonV3f(j["color2"]);
    if (j.contains("color3"))
        sky->color3_ = fromJsonV3f(j["color3"]);
    if (j.contains("hdri") && !j["hdri"].is_null())
    {
        const std::string path = j["hdri"].get<std::string>();
        auto handle = ctx._assetManager->getHandle<Texture>(path);
        if (!handle)
            if (auto any = loadAsset(path, ctx))
                if (auto* th = std::get_if<TextureHandle>(&*any))
                    handle = *th;
        if (handle)
            sky->hdri_ = *handle;
    }
}

// --- Handler table -----------------------------------------------------------

std::span<const ComponentHandler> getComponentHandlers()
{
    static const std::array<ComponentHandler, 5> handlers = {{
        {"transform", 1, serializeTransform, deserializeTransform, extractTransform},
        {"mesh", 1, serializeMesh, deserializeMesh, extractMesh},
        {"materials", 1, serializeMaterials, deserializeMaterials, extractMaterials},
        {"camera", 1, serializeCamera, deserializeCamera, extractCamera},
        {"skybox", 1, serializeSkybox, deserializeSkybox, extractSkybox},
    }};
    return handlers;
}

std::string_view componentTypeName(const ComponentDesc& d)
{
    return std::visit(
        [](const auto& c) -> std::string_view
        {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, Transform_C>)
                return "transform";
            if constexpr (std::is_same_v<T, MeshDesc>)
                return "mesh";
            if constexpr (std::is_same_v<T, MaterialsDesc>)
                return "materials";
            if constexpr (std::is_same_v<T, Camera_C>)
                return "camera";
            if constexpr (std::is_same_v<T, SkyboxDesc>)
                return "skybox";
            return "";
        },
        d);
}

}  // namespace batap
