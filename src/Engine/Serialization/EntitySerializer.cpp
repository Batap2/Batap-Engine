#include "EntitySerializer.h"
#include "EntityDescSerializer.h"

#include "Components/Hierarchy_C.h"
#include "Components/Kind_C.h"
#include "Components/Name_C.h"
#include "Engine.h"
#include "Instance/EntityFactory.h"
#include "Instance/InstanceManager.h"
#include "Reflection/ComponentRegistry.h"
#include "Scene.h"
#include "Systems/Hierarchy_S.h"
#include "World.h"

#include <entt/entt.hpp>
#include <nlohmann/json.hpp>

#include <fstream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace batap
{

static void collectDFS(entt::registry& reg, entt::entity e, std::vector<entt::entity>& order,
                       std::unordered_map<uint32_t, int>& indexMap)
{
    indexMap[entt::to_integral(e)] = static_cast<int>(order.size());
    order.push_back(e);
    EntityHandle h{&reg, e};
    for (entt::entity child : Hierarchy_S::children(h))
        collectDFS(reg, child, order, indexMap);
}

// All registry-declared components of h, serialized field by field.
static nlohmann::json reflectedComponents(EntityHandle h, const Engine& ctx)
{
    nlohmann::json arr = nlohmann::json::array();
    for (const ComponentType& t : ComponentRegistry::instance().all())
    {
        void* c = t.tryGet(*h.reg_, h.entity_);
        if (!c)
            continue;
        nlohmann::json cj;
        for (const Field& f : t.fields)
            f.type->toJson(f.ptrIn(c), cj[f.name], ctx);
        cj["type"] = t.name;
        arr.push_back(std::move(cj));
    }
    return arr;
}

static const char* kindName(EntityKind k)
{
    switch (k)
    {
        case EntityKind::Empty:
            return "empty";
        case EntityKind::StaticMesh:
            return "mesh";
        case EntityKind::Camera:
            return "camera";
        case EntityKind::PointLight:
            return "pointLight";
        case EntityKind::Skybox:
            return "skybox";
    }
    return "empty";
}

// Written for forward compatibility; nothing reads it back yet — a migration
// would branch on it in the field loop of populateWorld().
static void writeFile(nlohmann::json& root, const std::unordered_set<std::string>& usedTypes,
                      const std::string& path)
{
    auto& versionsJ = root["componentVersions"] = nlohmann::json::object();
    for (const auto& t : ComponentRegistry::instance().all())
        if (usedTypes.contains(t.name))
            versionsJ[t.name] = t.meta.version;

    std::ofstream f(path);
    f << root.dump(2);
}

void EntitySerializer::save(World& world, const Engine& ctx, const std::string& path)
{
    auto& reg = world.scene_->registry_;

    std::vector<entt::entity> order;
    std::unordered_map<uint32_t, int> indexMap;

    for (auto e : reg.storage<entt::entity>())
    {
        auto* hc = reg.try_get<Hierarchy_C>(e);
        if (!hc || hc->parent == entt::null)
            collectDFS(reg, e, order, indexMap);
    }

    nlohmann::json root;
    std::unordered_set<std::string> usedTypes;
    auto& entitiesJ = root["entities"] = nlohmann::json::array();

    for (auto e : order)
    {
        EntityHandle h{&reg, e};
        nlohmann::json ej;

        auto* nc = reg.try_get<Name_C>(e);
        ej["name"] = nc ? nc->name_ : std::string{};

        auto* k = reg.try_get<Kind_C>(e);
        ej["kind"] = k ? kindName(k->value) : "empty";

        ej["parent"] = nlohmann::json(nullptr);
        if (auto* hc = reg.try_get<Hierarchy_C>(e); hc && hc->parent != entt::null)
            if (auto it = indexMap.find(entt::to_integral(hc->parent)); it != indexMap.end())
                ej["parent"] = it->second;

        auto compsJ = reflectedComponents(h, ctx);
        for (const auto& cj : compsJ)
            usedTypes.emplace(cj["type"].get<std::string>());
        ej["components"] = std::move(compsJ);

        entitiesJ.push_back(std::move(ej));
    }

    writeFile(root, usedTypes, path);
}

// Importer path: descs carry asset paths because no handle exists yet.
void EntitySerializer::save(const std::vector<EntityDesc>& entities, const std::string& path)
{
    nlohmann::json root;
    std::unordered_set<std::string> usedTypes;
    auto& entitiesJ = root["entities"] = nlohmann::json::array();

    for (const auto& desc : entities)
    {
        nlohmann::json ej;
        ej["name"] = desc.name;
        ej["kind"] = desc.kind;
        ej["parent"] =
            desc.parentIndex >= 0 ? nlohmann::json(desc.parentIndex) : nlohmann::json(nullptr);

        auto& compsJ = ej["components"] = nlohmann::json::array();
        for (const auto& comp : desc.components)
        {
            std::string_view type = componentTypeName(comp);
            std::visit(
                [&](const auto& c)
                {
                    nlohmann::json cj = toJson(c);
                    cj["type"] = type;
                    usedTypes.emplace(type);
                    compsJ.push_back(std::move(cj));
                },
                comp);
        }

        entitiesJ.push_back(std::move(ej));
    }

    writeFile(root, usedTypes, path);
}

static void populateWorld(World& world, const Engine& ctx, const nlohmann::json& root)
{
    if (!root.contains("entities"))
        return;

    auto& reg = world.scene_->registry_;
    auto& factory = *world.entityFactory_;

    const auto& entitiesJ = root["entities"];
    std::vector<entt::entity> created;
    created.reserve(entitiesJ.size());

    // Pass 1 — create entities and apply components
    for (const auto& ej : entitiesJ)
    {
        const auto& compsJ = ej.contains("components") ? ej["components"] : nlohmann::json::array();

        const std::string kind = ej.value("kind", "empty");

        EntityHandle h;
        if (kind == "mesh")
            h = factory.createStaticMesh(reg);
        else if (kind == "pointLight")
            h = factory.createPointLight(reg);
        else if (kind == "camera")
            h = factory.createCamera(reg);
        else if (kind == "skybox")
            h = factory.createSkybox(reg);
        else
            h = factory.createEmpty(reg);

        if (auto* nc = reg.try_get<Name_C>(h.entity_))
            nc->name_ = ej.value("name", "");

        for (const auto& cj : compsJ)
        {
            const ComponentType* ct = ComponentRegistry::instance().find(cj.value("type", ""));
            if (!ct)
                continue;

            void* c = ct->getOrEmplace(reg, h.entity_);
            for (const Field& f : ct->fields)
                if (cj.contains(f.name))
                    f.type->fromJson(f.ptrIn(c), cj[f.name], ctx);

            if (ct->meta.onDeserialized)
                ct->meta.onDeserialized(h, world);
            world.instanceManager_->markDirty(h, ct->meta.flag);
        }

        created.push_back(h.entity_);
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

void EntitySerializer::clearSceneAndLoad(World& world, const Engine& ctx, const std::string& path)
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

    auto& reg = world.scene_->registry_;
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

void EntitySerializer::instantiate(World& world, const Engine& ctx, const std::string& path)
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

    populateWorld(world, ctx, root);
}
}  // namespace batap
