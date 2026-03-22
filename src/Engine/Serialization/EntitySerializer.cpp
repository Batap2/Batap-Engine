#include "EntitySerializer.h"
#include "ComponentSerializers.h"

#include "Components/Hierarchy_C.h"
#include "Components/Name_C.h"
#include "Context.h"
#include "Instance/EntityFactory.h"
#include "Scene.h"
#include "Systems/Hierarchy_S.h"
#include "World.h"

#include <entt/entt.hpp>
#include <nlohmann/json.hpp>

#include <fstream>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace batap
{

// --- helpers -----------------------------------------------------------------

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

static std::string_view kindFromComponents(const nlohmann::json& compsJ)
{
    for (const auto& c : compsJ)
    {
        std::string type = c.value("type", "");
        if (type == "mesh")       return "mesh";
        if (type == "pointLight") return "pointLight";
        if (type == "camera")     return "camera";
    }
    return "empty";
}

// --- save --------------------------------------------------------------------

void EntitySerializer::save(World& world, const Context& ctx, const std::string& path)
{
    auto& reg = world.scene_->_registry;

    std::vector<entt::entity> order;
    std::unordered_map<uint32_t, int> indexMap;

    for (auto e : reg.storage<entt::entity>())
    {
        auto* hc = reg.try_get<Hierarchy_C>(e);
        if (!hc || hc->parent == entt::null)
            collectDFS(reg, e, order, indexMap);
    }

    nlohmann::json root;

    const auto handlers = getComponentHandlers();

    auto& versionsJ = root["componentVersions"] = nlohmann::json::object();
    for (const auto& handler : handlers)
        versionsJ[std::string(handler.type)] = handler.currentVersion;

    auto& entitiesJ = root["entities"] = nlohmann::json::array();

    for (auto e : order)
    {
        nlohmann::json ej;
        EntityHandle h{&reg, e};

        if (auto* nc = reg.try_get<Name_C>(e))
            ej["name"] = nc->_name;
        else
            ej["name"] = "";

        auto& compsJ = ej["components"] = nlohmann::json::array();
        for (const auto& handler : handlers)
        {
            if (auto props = handler.serialize(h, ctx))
            {
                (*props)["type"] = handler.type;
                compsJ.push_back(std::move(*props));
            }
        }

        auto* hc = reg.try_get<Hierarchy_C>(e);
        if (hc && hc->parent != entt::null)
        {
            auto it = indexMap.find(entt::to_integral(hc->parent));
            ej["parent"] = (it != indexMap.end()) ? nlohmann::json(it->second) : nlohmann::json(nullptr);
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

// --- populateWorld -----------------------------------------------------------

static void populateWorld(World& world, const Context& ctx, const nlohmann::json& root)
{
    if (!root.contains("entities"))
        return;

    std::unordered_map<std::string, uint32_t> versions;
    if (root.contains("componentVersions"))
        for (auto& [k, v] : root["componentVersions"].items())
            versions[k] = v.get<uint32_t>();

    auto& reg     = world.scene_->_registry;
    auto& factory = *world.entityFactory_;

    const auto& entitiesJ = root["entities"];
    std::vector<entt::entity> created;
    created.reserve(entitiesJ.size());

    // Pass 1 — create entities and apply components
    for (const auto& ej : entitiesJ)
    {
        const auto& compsJ = ej.contains("components")
                           ? ej["components"]
                           : nlohmann::json::array();

        std::string_view kind = kindFromComponents(compsJ);

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
            h = {&reg, e};
        }

        if (auto* nc = reg.try_get<Name_C>(h._entity))
            nc->_name = ej.value("name", "");

        for (const auto& cj : compsJ)
        {
            std::string type = cj.value("type", "");
            uint32_t version = 1;
            auto it = versions.find(type);
            if (it != versions.end()) version = it->second;

            for (const auto& handler : getComponentHandlers())
            {
                if (handler.type == type)
                {
                    handler.deserialize(h, cj, version, ctx, world);
                    break;
                }
            }
        }

        created.push_back(h._entity);
    }

    // Pass 2 — hierarchy
    for (size_t i = 0; i < entitiesJ.size(); ++i)
    {
        const auto& ej = entitiesJ[i];
        if (ej.contains("parent") && !ej["parent"].is_null())
        {
            int parentIdx = ej["parent"].get<int>();
            if (parentIdx >= 0 && parentIdx < static_cast<int>(created.size()))
                Hierarchy_S::attach({&reg, created[static_cast<size_t>(parentIdx)]},
                                    {&reg, created[i]});
        }
    }
}

// --- load --------------------------------------------------------------------

void EntitySerializer::load(World& world, const Context& ctx, const std::string& path)
{
    std::ifstream f(path);
    if (!f.is_open()) return;

    nlohmann::json root;
    try { root = nlohmann::json::parse(f); }
    catch (...) { return; }

    auto& reg     = world.scene_->_registry;
    auto& factory = *world.entityFactory_;

    std::vector<entt::entity> roots;
    for (auto e : reg.storage<entt::entity>())
    {
        auto* hc = reg.try_get<Hierarchy_C>(e);
        if (!hc || hc->parent == entt::null)
            roots.push_back(e);
    }
    for (auto e : roots)
        factory.destroy({&reg, e});

    populateWorld(world, ctx, root);
}

// --- instantiate -------------------------------------------------------------

void EntitySerializer::instantiate(World& world, const Context& ctx, const std::string& path)
{
    std::ifstream f(path);
    if (!f.is_open()) return;

    nlohmann::json root;
    try { root = nlohmann::json::parse(f); }
    catch (...) { return; }

    populateWorld(world, ctx, root);
}

// --- saveTemplate ------------------------------------------------------------

void EntitySerializer::saveTemplate(const std::vector<EntityDesc>& entities, const std::string& path)
{
    nlohmann::json root;

    auto& entitiesJ = root["entities"] = nlohmann::json::array();
    std::unordered_set<std::string> usedTypes;

    for (const auto& desc : entities)
    {
        nlohmann::json ej;
        ej["name"]   = desc.name;
        ej["parent"] = desc.parentIndex >= 0 ? nlohmann::json(desc.parentIndex) : nlohmann::json(nullptr);

        auto& compsJ = ej["components"] = nlohmann::json::array();
        for (const auto& comp : desc.components)
        {
            std::visit([&](const auto& c)
            {
                using T = std::decay_t<decltype(c)>;
                nlohmann::json cj = toJson(c);
                std::string_view type;
                if constexpr (std::is_same_v<T, TransformDesc>)
                    type = "transform";
                else if constexpr (std::is_same_v<T, MeshDesc>)
                    type = "mesh";
                cj["type"] = type;
                usedTypes.emplace(type);
                compsJ.push_back(std::move(cj));
            }, comp);
        }

        entitiesJ.push_back(std::move(ej));
    }

    auto& versionsJ = root["componentVersions"] = nlohmann::json::object();
    for (const auto& handler : getComponentHandlers())
        if (usedTypes.contains(std::string(handler.type)))
            versionsJ[std::string(handler.type)] = handler.currentVersion;

    std::ofstream f(path);
    f << root.dump(2);
}

}  // namespace batap
