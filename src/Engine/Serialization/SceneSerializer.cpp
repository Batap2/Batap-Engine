#include "SceneSerializer.h"

#include "Assets/AssetManager.h"
#include "Assets/AssetSlotMap.h"
#include "Assets/Mesh.h"
#include "Components/Camera_C.h"
#include "Components/Hierarchy_C.h"
#include "Components/Mesh_C.h"
#include "Components/Name_C.h"
#include "Components/PointLight_C.h"
#include "Components/Transform_C.h"
#include "Context.h"
#include "Instance/EntityFactory.h"
#include "Scene.h"
#include "Systems/Hierarchy_S.h"
#include "Systems/Systems.h"
#include "Systems/Transform_S.h"
#include "World.h"

#include <entt/entt.hpp>
#include <nlohmann/json.hpp>

#include <fstream>
#include <unordered_map>
#include <vector>

namespace batap
{

// --- helpers -----------------------------------------------------------------

static nlohmann::json serializeV3(const v3f& v)
{
    return {v.x(), v.y(), v.z()};
}

static nlohmann::json serializeQuat(const quatf& q)
{
    return {q.x(), q.y(), q.z(), q.w()};
}

static v3f deserializeV3(const nlohmann::json& j)
{
    return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>()};
}

static quatf deserializeQuat(const nlohmann::json& j)
{
    // Eigen ctor: (w, x, y, z), stored as [x, y, z, w]
    return {j[3].get<float>(), j[0].get<float>(), j[1].get<float>(), j[2].get<float>()};
}

// DFS collect: roots first, then their children recursively
static void collectDFS(entt::registry& reg, entt::entity e,
                       std::vector<entt::entity>& order,
                       std::unordered_map<uint32_t, int>& indexMap)
{
    indexMap[entt::to_integral(e)] = static_cast<int>(order.size());
    order.push_back(e);

    EntityHandle h{&reg, e};
    for (entt::entity child : Hierarchy_S::children(h))
        collectDFS(reg, child, order, indexMap);
}

// --- save --------------------------------------------------------------------

void SceneSerializer::save(World& world, Context& ctx, const std::string& path)
{
    auto& reg = world.scene_->_registry;
    auto& assetMgr = *ctx._assetManager;

    // Collect entities in hierarchical order (roots first)
    std::vector<entt::entity> order;
    std::unordered_map<uint32_t, int> indexMap;

    for (auto e : reg.storage<entt::entity>())
    {
        auto* hc = reg.try_get<Hierarchy_C>(e);
        if (!hc || hc->parent == entt::null)
            collectDFS(reg, e, order, indexMap);
    }

    nlohmann::json root;
    auto& entitiesJ = root["entities"] = nlohmann::json::array();

    for (auto e : order)
    {
        nlohmann::json ej;

        // Name
        if (auto* nc = reg.try_get<Name_C>(e))
            ej["name"] = nc->_name;
        else
            ej["name"] = "";

        // Kind
        if (reg.try_get<Mesh_C>(e))
            ej["kind"] = "mesh";
        else if (reg.try_get<PointLight_C>(e))
            ej["kind"] = "pointLight";
        else if (reg.try_get<Camera_C>(e))
            ej["kind"] = "camera";
        else
            ej["kind"] = "empty";

        // Transform
        if (auto* tc = reg.try_get<Transform_C>(e))
        {
            ej["transform"]["pos"]   = serializeV3(tc->pos());
            ej["transform"]["rot"]   = serializeQuat(tc->rot());
            ej["transform"]["scale"] = serializeV3(tc->scale());
        }

        // Mesh
        if (auto* mc = reg.try_get<Mesh_C>(e))
        {
            if (mc->_mesh)
            {
                if (auto* p = assetMgr.getPath<Mesh>(mc->_mesh))
                    ej["mesh"] = *p;
                else
                    ej["mesh"] = nullptr;
            }
            else
            {
                ej["mesh"] = nullptr;
            }
        }

        // PointLight
        if (auto* pl = reg.try_get<PointLight_C>(e))
        {
            ej["pointLight"]["color"]       = serializeV3(pl->color_);
            ej["pointLight"]["intensity"]   = pl->intensity_;
            ej["pointLight"]["radius"]      = pl->radius_;
            ej["pointLight"]["falloff"]     = pl->falloff_;
            ej["pointLight"]["castShadows"] = pl->castShadows_;
        }

        // Camera
        if (auto* cam = reg.try_get<Camera_C>(e))
        {
            ej["camera"]["znear"]  = cam->_znear;
            ej["camera"]["zfar"]   = cam->_zfar;
            ej["camera"]["fov"]    = cam->_fov;
            ej["camera"]["active"] = cam->_active;
        }

        // Parent index
        auto* hc = reg.try_get<Hierarchy_C>(e);
        if (hc && hc->parent != entt::null)
        {
            auto it = indexMap.find(entt::to_integral(hc->parent));
            if (it != indexMap.end())
                ej["parent"] = it->second;
            else
                ej["parent"] = nullptr;
        }
        else
        {
            ej["parent"] = nullptr;
        }

        entitiesJ.push_back(std::move(ej));
    }

    std::ofstream f(path);
    f << root.dump(2);
}

// --- load --------------------------------------------------------------------

void SceneSerializer::load(World& world, Context& ctx, const std::string& path)
{
    std::ifstream f(path);
    if (!f.is_open())
        return;

    nlohmann::json root;
    try
    {
        root = nlohmann::json::parse(f);
    }
    catch (...)
    {
        return;
    }

    auto& reg = world.scene_->_registry;
    auto& assetMgr = *ctx._assetManager;
    auto& transforms = *world.systems_->_transforms;
    auto& factory = *world.entityFactory_;

    // Clear current scene (destroy root entities, they destroy children recursively)
    {
        std::vector<entt::entity> roots;
        for (auto e : reg.storage<entt::entity>())
        {
            auto* hc = reg.try_get<Hierarchy_C>(e);
            if (!hc || hc->parent == entt::null)
                roots.push_back(e);
        }
        for (auto e : roots)
            factory.destroy({&reg, e});
    }

    if (!root.contains("entities"))
        return;

    auto& entitiesJ = root["entities"];
    std::vector<entt::entity> created;
    created.reserve(entitiesJ.size());

    // Pass 1 — create entities
    for (auto& ej : entitiesJ)
    {
        std::string kind = ej.value("kind", "empty");
        EntityHandle h;

        if (kind == "mesh")
            h = factory.createStaticMesh(reg);
        else if (kind == "pointLight")
            h = factory.createPointLight(reg);
        else if (kind == "camera")
            h = factory.createCamera(reg);
        else
        {
            auto e = reg.create();
            reg.emplace<Name_C>(e);
            reg.emplace<Transform_C>(e);
            h = {&reg, e};
        }

        // Name
        if (auto* nc = reg.try_get<Name_C>(h._entity))
            nc->_name = ej.value("name", "");

        // Transform
        if (ej.contains("transform"))
        {
            auto& tj = ej["transform"];
            EntityHandle eh{&reg, h._entity};
            transforms.setLocalPosition(eh, deserializeV3(tj["pos"]));
            transforms.setLocalRotation(eh, deserializeQuat(tj["rot"]));
            transforms.setLocalScale(eh, deserializeV3(tj["scale"]));
        }

        // Mesh handle
        if (ej.contains("mesh") && !ej["mesh"].is_null())
        {
            if (auto* mc = reg.try_get<Mesh_C>(h._entity))
            {
                if (auto handle = assetMgr.getHandle<Mesh>(ej["mesh"].get<std::string>()))
                    mc->_mesh = *handle;
            }
        }

        // PointLight
        if (ej.contains("pointLight"))
        {
            if (auto* pl = reg.try_get<PointLight_C>(h._entity))
            {
                auto& pj = ej["pointLight"];
                pl->color_       = deserializeV3(pj["color"]);
                pl->intensity_   = pj["intensity"].get<float>();
                pl->radius_      = pj["radius"].get<float>();
                pl->falloff_     = pj["falloff"].get<float>();
                pl->castShadows_ = pj["castShadows"].get<bool>();
            }
        }

        // Camera
        if (ej.contains("camera"))
        {
            if (auto* cam = reg.try_get<Camera_C>(h._entity))
            {
                auto& cj = ej["camera"];
                cam->_znear  = cj["znear"].get<float>();
                cam->_zfar   = cj["zfar"].get<float>();
                cam->_fov    = cj["fov"].get<float>();
                cam->_active = cj["active"].get<bool>();
            }
        }

        created.push_back(h._entity);
    }

    // Pass 2 — attach hierarchy
    for (size_t i = 0; i < entitiesJ.size(); ++i)
    {
        auto& ej = entitiesJ[i];
        if (ej.contains("parent") && !ej["parent"].is_null())
        {
            int parentIdx = ej["parent"].get<int>();
            if (parentIdx >= 0 && parentIdx < static_cast<int>(created.size()))
            {
                EntityHandle parent{&reg, created[static_cast<size_t>(parentIdx)]};
                EntityHandle child{&reg, created[i]};
                Hierarchy_S::attach(parent, child);
            }
        }
    }
}

}  // namespace batap
