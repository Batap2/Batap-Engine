#include "ComponentSerializers.h"
#include "EntityDesc.h"

#include "Assets/AssetLoader.h"
#include "Assets/AssetManager.h"
#include "Assets/Mesh.h"
#include "Components/Camera_C.h"
#include "Components/Mesh_C.h"
#include "Components/PointLight_C.h"
#include "Components/Transform_C.h"
#include "Context.h"
#include "Systems/Systems.h"
#include "Systems/Transform_S.h"
#include "World.h"

#include <array>
#include <entt/entt.hpp>

namespace batap
{

// --- helpers -----------------------------------------------------------------

static nlohmann::json toJson(const v3f& v)   { return {v.x(), v.y(), v.z()}; }
static nlohmann::json toJson(const quatf& q) { return {q.x(), q.y(), q.z(), q.w()}; }

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
    return {
        {"pos",   toJson(c.pos())},
        {"rot",   toJson(c.rot())},
        {"scale", toJson(c.scale())}
    };
}

static std::optional<ComponentDesc> extractTransform(EntityHandle h, const Context&)
{
    auto* tc = h._reg->try_get<Transform_C>(h._entity);
    if (!tc) return std::nullopt;
    return *tc;
}

static std::optional<nlohmann::json> serializeTransform(EntityHandle h, const Context& ctx)
{
    if (auto d = extractTransform(h, ctx)) return toJson(std::get<Transform_C>(*d));
    return std::nullopt;
}

static void deserializeTransform(EntityHandle h, const nlohmann::json& j,
                                  uint32_t /*version*/, const Context&, World& world)
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

static std::optional<ComponentDesc> extractMesh(EntityHandle h, const Context& ctx)
{
    auto* mc = h._reg->try_get<Mesh_C>(h._entity);
    if (!mc) return std::nullopt;
    MeshDesc desc;
    if (mc->_mesh)
        if (auto* p = ctx._assetManager->getPath<Mesh>(mc->_mesh))
            desc.path = *p;
    return desc;
}

static std::optional<nlohmann::json> serializeMesh(EntityHandle h, const Context& ctx)
{
    if (auto d = extractMesh(h, ctx)) return toJson(std::get<MeshDesc>(*d));
    return std::nullopt;
}

static void deserializeMesh(EntityHandle h, const nlohmann::json& j,
                             uint32_t /*version*/, const Context& ctx, World&)
{
    auto* mc = h._reg->try_get<Mesh_C>(h._entity);
    if (!mc) return;
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

// --- PointLight --------------------------------------------------------------

nlohmann::json toJson(const PointLight_C& c)
{
    return {
        {"color",       toJson(c.color_)},
        {"intensity",   c.intensity_},
        {"radius",      c.radius_},
        {"falloff",     c.falloff_},
        {"castShadows", c.castShadows_}
    };
}

static std::optional<ComponentDesc> extractPointLight(EntityHandle h, const Context&)
{
    auto* pl = h._reg->try_get<PointLight_C>(h._entity);
    if (!pl) return std::nullopt;
    return *pl;
}

static std::optional<nlohmann::json> serializePointLight(EntityHandle h, const Context& ctx)
{
    if (auto d = extractPointLight(h, ctx)) return toJson(std::get<PointLight_C>(*d));
    return std::nullopt;
}

static void deserializePointLight(EntityHandle h, const nlohmann::json& j,
                                   uint32_t /*version*/, const Context&, World&)
{
    auto* pl = h._reg->try_get<PointLight_C>(h._entity);
    if (!pl) return;
    pl->color_       = fromJsonV3f(j["color"]);
    pl->intensity_   = j["intensity"].get<float>();
    pl->radius_      = j["radius"].get<float>();
    pl->falloff_     = j["falloff"].get<float>();
    pl->castShadows_ = j["castShadows"].get<bool>();
}

// --- Camera ------------------------------------------------------------------

nlohmann::json toJson(const Camera_C& c)
{
    return {
        {"znear",  c._znear},
        {"zfar",   c._zfar},
        {"fov",    c._fov},
        {"active", c._active}
    };
}

static std::optional<ComponentDesc> extractCamera(EntityHandle h, const Context&)
{
    auto* cam = h._reg->try_get<Camera_C>(h._entity);
    if (!cam) return std::nullopt;
    return *cam;
}

static std::optional<nlohmann::json> serializeCamera(EntityHandle h, const Context& ctx)
{
    if (auto d = extractCamera(h, ctx)) return toJson(std::get<Camera_C>(*d));
    return std::nullopt;
}

static void deserializeCamera(EntityHandle h, const nlohmann::json& j,
                               uint32_t /*version*/, const Context&, World&)
{
    auto* cam = h._reg->try_get<Camera_C>(h._entity);
    if (!cam) return;
    cam->_znear  = j["znear"].get<float>();
    cam->_zfar   = j["zfar"].get<float>();
    cam->_fov    = j["fov"].get<float>();
    cam->_active = j["active"].get<bool>();
}

// --- Handler table -----------------------------------------------------------

std::span<const ComponentHandler> getComponentHandlers()
{
    static const std::array<ComponentHandler, 4> handlers = {{
        {"transform", 1, serializeTransform,  deserializeTransform,  extractTransform },
        {"mesh",      1, serializeMesh,       deserializeMesh,       extractMesh      },
        {"pointLight",1, serializePointLight, deserializePointLight, extractPointLight},
        {"camera",    1, serializeCamera,     deserializeCamera,     extractCamera    },
    }};
    return handlers;
}

}  // namespace batap
