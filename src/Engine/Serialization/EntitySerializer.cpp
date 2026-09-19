#include "EntitySerializer.h"
#include "EntityDescSerializer.h"

#include "Components/EditorOnly_C.h"
#include "Components/Hierarchy_C.h"
#include "Components/Name_C.h"
#include "Components/UnknownComponents_C.h"
#include "Engine.h"
#include "Instance/EntityFactory.h"
#include "Instance/InstanceManager.h"
#include "Reflection/ComponentRegistry.h"
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
        if (!t.tryGet)
            continue;
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

// Written for forward compatibility; nothing reads it back yet — a migration
// would branch on it in the field loop of populateWorld().
static void writeVersions(nlohmann::json& root, const std::unordered_set<std::string>& usedTypes)
{
    auto& versionsJ = root["componentVersions"] = nlohmann::json::object();
    for (const auto& t : ComponentRegistry::instance().all())
        if (usedTypes.contains(t.name))
            versionsJ[t.name] = t.meta.version;
}

static void writeFile(nlohmann::json& root, const std::unordered_set<std::string>& usedTypes,
                      const std::string& path)
{
    writeVersions(root, usedTypes);

    std::ofstream f(path);
    f << root.dump(2);
}

static nlohmann::json sceneToJson(World& world, const Engine& ctx)
{
    auto& reg = world.registry_;

    std::vector<entt::entity> order;
    std::unordered_map<uint32_t, int> indexMap;

    for (auto e : reg.storage<entt::entity>())
    {
        if (!reg.valid(e) || reg.all_of<EditorOnly_C>(e))
            continue;
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

        ej["parent"] = nlohmann::json(nullptr);
        if (auto* hc = reg.try_get<Hierarchy_C>(e); hc && hc->parent != entt::null)
            if (auto it = indexMap.find(entt::to_integral(hc->parent)); it != indexMap.end())
                ej["parent"] = it->second;

        auto compsJ = reflectedComponents(h, ctx);
        for (const auto& cj : compsJ)
            usedTypes.emplace(cj["type"].get<std::string>());

        if (auto* uc = reg.try_get<UnknownComponents_C>(e))
            for (const std::string& blob : uc->blobs_)
                compsJ.push_back(nlohmann::json::parse(blob));

        ej["components"] = std::move(compsJ);

        entitiesJ.push_back(std::move(ej));
    }

    writeVersions(root, usedTypes);
    return root;
}

void EntitySerializer::save(World& world, const Engine& ctx, const std::string& path)
{
    std::ofstream f(path);
    f << sceneToJson(world, ctx).dump(2);
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

    auto& reg = world.registry_;
    auto& factory = world.factory();

    const auto& entitiesJ = root["entities"];
    std::vector<entt::entity> created;
    created.reserve(entitiesJ.size());

    // Pass 1 — create entities and apply components
    for (const auto& ej : entitiesJ)
    {
        const auto& compsJ = ej.contains("components") ? ej["components"] : nlohmann::json::array();

        // The components carry everything: a marker component puts the entity
        // in its GPU pool as it is emplaced.
        EntityHandle h = factory.create(reg, Spawnables[0]);

        if (auto* nc = reg.try_get<Name_C>(h.entity_))
            nc->name_ = ej.value("name", "");

        for (const auto& cj : compsJ)
        {
            const ComponentType* ct = ComponentRegistry::instance().find(cj.value("type", ""));
            if (!ct || !ct->getOrEmplace)
            {
                reg.get_or_emplace<UnknownComponents_C>(h.entity_).blobs_.push_back(cj.dump());
                continue;
            }

            void* c = ct->getOrEmplace(reg, h.entity_);
            for (const Field& f : ct->fields)
                if (cj.contains(f.name))
                    f.type->fromJson(f.ptrIn(c), cj[f.name], ctx);

            if (ct->meta.onDeserialized)
                ct->meta.onDeserialized(h, world);
            world.instances().markDirty(h, ct->mask());
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

static void clearScene(World& world)
{
    auto& reg = world.registry_;
    auto& factory = world.factory();

    std::vector<entt::entity> roots;
    for (auto e : reg.storage<entt::entity>())
    {
        if (!reg.valid(e))
            continue;
        auto* hc = reg.try_get<Hierarchy_C>(e);
        if (!hc || hc->parent == entt::null)
            roots.push_back(e);
    }
    for (auto e : roots)
        factory.destroy({&reg, e});
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

    clearScene(world);
    populateWorld(world, ctx, root);
}

std::string EntitySerializer::toBuffer(World& world, const Engine& ctx)
{
    return sceneToJson(world, ctx).dump();
}

void EntitySerializer::clearSceneAndLoadBuffer(World& world, const Engine& ctx,
                                               const std::string& buffer)
{
    nlohmann::json root;
    try
    {
        root = nlohmann::json::parse(buffer);
    }
    catch (...)
    {
        return;
    }

    clearScene(world);
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
