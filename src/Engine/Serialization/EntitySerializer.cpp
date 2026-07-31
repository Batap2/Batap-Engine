#include "EntitySerializer.h"
#include "ComponentSerializers.h"

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
        void* c = t.tryGet(*h._reg, h._entity);
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

// reflectedOut, when given, receives one json array per entity (same order
// as the returned descs) with the registry-declared components.
static std::vector<EntityDesc> toEntityDescs(World& world, const Engine& ctx,
                                             std::vector<nlohmann::json>* reflectedOut = nullptr)
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

    std::vector<EntityDesc> descs;
    descs.reserve(order.size());

    for (auto e : order)
    {
        EntityDesc desc;
        EntityHandle h{&reg, e};

        if (auto* nc = reg.try_get<Name_C>(e))
            desc.name = nc->_name;

        if (auto* k = reg.try_get<Kind_C>(e))
        {
            switch (k->value)
            {
                case EntityKind::Empty:
                    desc.kind = "empty";
                    break;
                case EntityKind::StaticMesh:
                    desc.kind = "mesh";
                    break;
                case EntityKind::Camera:
                    desc.kind = "camera";
                    break;
                case EntityKind::PointLight:
                    desc.kind = "pointLight";
                    break;
                case EntityKind::Skybox:
                    desc.kind = "skybox";
                    break;
            }
        }

        auto* hc = reg.try_get<Hierarchy_C>(e);
        if (hc && hc->parent != entt::null)
        {
            auto it = indexMap.find(entt::to_integral(hc->parent));
            if (it != indexMap.end())
                desc.parentIndex = it->second;
        }

        for (const auto& handler : getComponentHandlers())
            if (auto c = handler.extract(h, ctx))
                desc.components.push_back(std::move(*c));

        if (reflectedOut)
            reflectedOut->push_back(reflectedComponents(h, ctx));

        descs.push_back(std::move(desc));
    }

    return descs;
}

// reflected, when non-empty, is aligned with entities: one json array per
// entity holding its registry-declared components (already serialized).
static void serializeEntityDescs(const std::vector<EntityDesc>& entities, const std::string& path,
                                 const std::vector<nlohmann::json>& reflected = {})
{
    nlohmann::json root;
    std::unordered_set<std::string> usedTypes;

    auto& entitiesJ = root["entities"] = nlohmann::json::array();

    for (size_t i = 0; i < entities.size(); ++i)
    {
        const auto& desc = entities[i];

        nlohmann::json ej;
        ej["name"] = desc.name;
        ej["kind"] = desc.kind;
        ej["parent"] = desc.parentIndex >= 0 ? nlohmann::json(desc.parentIndex)
                                             : nlohmann::json(nullptr);

        auto& compsJ = ej["components"] = nlohmann::json::array();
        for (const auto& comp : desc.components)
        {
            std::string_view type = componentTypeName(comp);
            std::visit(
                [&](const auto& c)
                {
                    nlohmann::json cj = toJson(c);
                    cj["type"]        = type;
                    usedTypes.emplace(type);
                    compsJ.push_back(std::move(cj));
                },
                comp);
        }

        if (i < reflected.size())
            for (const auto& cj : reflected[i])
            {
                usedTypes.emplace(cj["type"].get<std::string>());
                compsJ.push_back(cj);
            }

        entitiesJ.push_back(std::move(ej));
    }

    auto& versionsJ = root["componentVersions"] = nlohmann::json::object();
    for (const auto& handler : getComponentHandlers())
        if (usedTypes.contains(std::string(handler.type)))
            versionsJ[std::string(handler.type)] = handler.currentVersion;
    for (const auto& t : ComponentRegistry::instance().all())
        if (usedTypes.contains(t.name))
            versionsJ[t.name] = t.meta.version;

    std::ofstream f(path);
    f << root.dump(2);
}

void EntitySerializer::save(World& world, const Engine& ctx, const std::string& path)
{
    std::vector<nlohmann::json> reflected;
    auto descs = toEntityDescs(world, ctx, &reflected);
    serializeEntityDescs(descs, path, reflected);
}

void EntitySerializer::save(const std::vector<EntityDesc>& entities, const std::string& path)
{
    serializeEntityDescs(entities, path);
}

static void populateWorld(World& world, const Engine& ctx, const nlohmann::json& root)
{
    if (!root.contains("entities"))
        return;

    std::unordered_map<std::string, uint32_t> versions;
    if (root.contains("componentVersions"))
        for (auto& [k, v] : root["componentVersions"].items())
            versions[k] = v.get<uint32_t>();

    auto& reg = world.scene_->_registry;
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

        if (auto* nc = reg.try_get<Name_C>(h._entity))
            nc->_name = ej.value("name", "");

        for (const auto& cj : compsJ)
        {
            std::string type = cj.value("type", "");
            uint32_t version = 1;
            auto it = versions.find(type);
            if (it != versions.end())
                version = it->second;

            bool handled = false;
            for (const auto& handler : getComponentHandlers())
            {
                if (handler.type == type)
                {
                    handler.deserialize(h, cj, version, ctx, world);
                    handled = true;
                    break;
                }
            }

            if (!handled)
                if (const ComponentType* ct = ComponentRegistry::instance().find(type))
                {
                    void* c = ct->getOrEmplace(reg, h._entity);
                    for (const Field& f : ct->fields)
                        if (cj.contains(f.name))
                            f.type->fromJson(f.ptrIn(c), cj[f.name], ctx);
                    if (ct->meta.onDeserialized)
                        ct->meta.onDeserialized(h, world);
                    if (any(ct->meta.flag))
                        world.instanceManager_->markDirty(h, ct->meta.flag);
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

    auto& reg = world.scene_->_registry;
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
