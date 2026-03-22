#include "ComponentSerializers.h"
#include "EntityDesc.h"

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

nlohmann::json toJson(const TransformDesc& d)
{
    return {
        {"pos",   toJson(d.pos)},
        {"rot",   toJson(d.rot)},
        {"scale", toJson(d.scale)}
    };
}

static std::optional<nlohmann::json> serializeTransform(EntityHandle h, const Context&)
{
    auto* tc = h._reg->try_get<Transform_C>(h._entity);
    if (!tc) return std::nullopt;
    return toJson(TransformDesc{tc->pos(), tc->rot(), tc->scale()});
}

static void deserializeTransform(EntityHandle h, const nlohmann::json& j,
                                  uint32_t /*version*/, const Context&, World& world)
{
    if (!h._reg->try_get<Transform_C>(h._entity)) return;
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

static std::optional<nlohmann::json> serializeMesh(EntityHandle h, const Context& ctx)
{
    auto* mc = h._reg->try_get<Mesh_C>(h._entity);
    if (!mc) return std::nullopt;
    MeshDesc desc;
    if (mc->_mesh)
        if (auto* p = ctx._assetManager->getPath<Mesh>(mc->_mesh))
            desc.path = *p;
    return toJson(desc);
}

static void deserializeMesh(EntityHandle h, const nlohmann::json& j,
                             uint32_t /*version*/, const Context& ctx, World&)
{
    auto* mc = h._reg->try_get<Mesh_C>(h._entity);
    if (!mc) return;
    if (j.contains("path") && !j["path"].is_null())
        if (auto handle = ctx._assetManager->getHandle<Mesh>(j["path"].get<std::string>()))
            mc->_mesh = *handle;
}

// --- PointLight --------------------------------------------------------------

static std::optional<nlohmann::json> serializePointLight(EntityHandle h, const Context&)
{
    auto* pl = h._reg->try_get<PointLight_C>(h._entity);
    if (!pl) return std::nullopt;
    return nlohmann::json{
        {"color",       toJson(pl->color_)},
        {"intensity",   pl->intensity_},
        {"radius",      pl->radius_},
        {"falloff",     pl->falloff_},
        {"castShadows", pl->castShadows_}
    };
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

static std::optional<nlohmann::json> serializeCamera(EntityHandle h, const Context&)
{
    auto* cam = h._reg->try_get<Camera_C>(h._entity);
    if (!cam) return std::nullopt;
    return nlohmann::json{
        {"znear",  cam->_znear},
        {"zfar",   cam->_zfar},
        {"fov",    cam->_fov},
        {"active", cam->_active}
    };
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
        {"transform", 1, serializeTransform,  deserializeTransform },
        {"mesh",      1, serializeMesh,       deserializeMesh      },
        {"pointLight",1, serializePointLight, deserializePointLight},
        {"camera",    1, serializeCamera,     deserializeCamera    },
    }};
    return handlers;
}

}  // namespace batap
