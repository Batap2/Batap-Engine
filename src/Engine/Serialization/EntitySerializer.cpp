#include "EntitySerializer.h"
#include "EntityDescSerializer.h"

#include "Assets/AssetLoader.h"
#include "Components/EditorOnly_C.h"
#include "Components/FileId_C.h"
#include "Components/Hierarchy_C.h"
#include "Components/Name_C.h"
#include "Components/TemplateInstance_C.h"
#include "Components/TemplateMember_C.h"
#include "Components/UnknownComponents_C.h"
#include "Engine.h"
#include "Instance/EntityFactory.h"
#include "Instance/InstanceManager.h"
#include "Reflection/ComponentRegistry.h"
#include "Serialization/TemplateInstances.h"
#include "Systems/Hierarchy_S.h"
#include "World.h"

#include <entt/entt.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace batap
{
namespace
{
nlohmann::json reflectedComponents(EntityHandle h, const FieldIO& io)
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
            f.type->toJson(f.ptrIn(c), cj[f.name], io);
        cj["type"] = t.name;
        arr.push_back(std::move(cj));
    }
    return arr;
}

void writeVersions(nlohmann::json& root, const std::unordered_set<std::string>& usedTypes)
{
    auto& versionsJ = root["componentVersions"] = nlohmann::json::object();
    for (const auto& t : ComponentRegistry::instance().all())
        if (usedTypes.contains(t.name))
            versionsJ[t.name] = t.meta.version;
}

void writeFile(nlohmann::json& root, const std::unordered_set<std::string>& usedTypes,
               const std::string& path)
{
    writeVersions(root, usedTypes);

    std::ofstream f(path);
    f << root.dump(2);
}

struct Entry
{
    entt::entity entity_ = entt::null;
    nlohmann::json parent_;
    uint32_t id_ = 0;
    std::unique_ptr<InstanceView> view_;
};

struct SceneWriter
{
    World& world_;
    const Engine& ctx_;
    std::vector<Entry> entries_;

    void writeTree(entt::entity e, nlohmann::json parentRef);
    void writeInstanceChildren(const InstanceView& view, size_t entryIndex, entt::entity node,
                               bool isRoot);
    void assignIds();
};

void SceneWriter::writeTree(entt::entity e, nlohmann::json parentRef)
{
    auto& reg = world_.registry_;
    const size_t index = entries_.size();
    if (reg.all_of<TemplateInstance_C>(e))
    {
        auto view = std::make_unique<InstanceView>(viewInstance(world_, ctx_, e));
        if (!view->flat_.linkable_)
            std::cerr << "[Template] " << view->path_ << ": " << view->flat_.why_
                      << " - the instance is saved without its overrides\n";
        entries_.push_back({e, std::move(parentRef), 0, std::move(view)});
        writeInstanceChildren(*entries_[index].view_, index, e, true);
        return;
    }
    entries_.push_back({e, std::move(parentRef), 0, nullptr});
    for (entt::entity c : Hierarchy_S::children({&reg, e}))
        writeTree(c, nlohmann::json(index));
}

void SceneWriter::writeInstanceChildren(const InstanceView& view, size_t entryIndex,
                                        entt::entity node, bool isRoot)
{
    auto& reg = world_.registry_;
    const InstanceMember* nodeMember = view.member(node);
    for (entt::entity c : Hierarchy_S::children({&reg, node}))
    {
        if (view.member(c))
        {
            writeInstanceChildren(view, entryIndex, c, false);
            continue;
        }
        nlohmann::json parentRef =
            (isRoot || !nodeMember)
                ? nlohmann::json(entryIndex)
                : nlohmann::json{{"instance", entryIndex}, {"at", nodeMember->chain_}};
        writeTree(c, std::move(parentRef));
    }
}

void SceneWriter::assignIds()
{
    auto& reg = world_.registry_;
    std::unordered_set<uint32_t> used;
    for (Entry& en : entries_)
        if (auto* f = reg.try_get<FileId_C>(en.entity_);
            f && f->id_ != 0 && used.insert(f->id_).second)
            en.id_ = f->id_;
    uint32_t next = 1;
    for (Entry& en : entries_)
    {
        if (en.id_ != 0)
            continue;
        while (used.contains(next))
            ++next;
        en.id_ = next;
        used.insert(next);
        reg.emplace_or_replace<FileId_C>(en.entity_, FileId_C{next});
    }
}

nlohmann::json sceneToJson(World& world, const Engine& ctx, bool withState)
{
    auto& reg = world.registry_;
    std::vector<entt::entity> roots;
    for (auto e : reg.storage<entt::entity>())
    {
        if (!reg.valid(e) || reg.all_of<EditorOnly_C>(e))
            continue;
        auto* hc = reg.try_get<Hierarchy_C>(e);
        if (!hc || hc->parent == entt::null)
            roots.push_back(e);
    }
    const auto fileOrder = [&](entt::entity e)
    {
        auto* f = reg.try_get<FileId_C>(e);
        return f && f->id_ != 0 ? f->id_ : UINT32_MAX;
    };
    std::stable_sort(roots.begin(), roots.end(),
                     [&](entt::entity a, entt::entity b) { return fileOrder(a) < fileOrder(b); });

    SceneWriter writer{world, ctx, {}};
    for (entt::entity e : roots)
        writer.writeTree(e, nlohmann::json(nullptr));
    writer.assignIds();

    std::vector<entt::entity> order;
    std::unordered_map<uint32_t, int> indexMap;
    order.reserve(writer.entries_.size());
    for (const Entry& en : writer.entries_)
    {
        indexMap[entt::to_integral(en.entity_)] = static_cast<int>(order.size());
        order.push_back(en.entity_);
    }

    const FieldIO io{ctx, order};
    nlohmann::json root;
    if (withState)
    {
        root["time"] = world.time_.simTime_;
        const Origin& origin = world.origin_;
        root["origin"] = {origin.pos_.x(), origin.pos_.y(), origin.pos_.z()};
        root["originOffset"] = {origin.offset_.x(), origin.offset_.y(), origin.offset_.z()};
        root["originAnchor"] = nlohmann::json(nullptr);
        if (auto it = indexMap.find(entt::to_integral(world.origin_.anchor_)); it != indexMap.end())
            root["originAnchor"] = it->second;
    }
    std::unordered_set<std::string> usedTypes;
    auto& entitiesJ = root["entities"] = nlohmann::json::array();

    for (const Entry& en : writer.entries_)
    {
        EntityHandle h{&reg, en.entity_};
        nlohmann::json ej;
        ej["id"] = en.id_;
        ej["parent"] = en.parent_;

        auto* nc = reg.try_get<Name_C>(en.entity_);
        const std::string name = nc ? nc->name_ : std::string{};

        if (en.view_)
        {
            const InstanceView& view = *en.view_;
            ej["template"] = view.path_;
            const FlatEntry* flatRoot = view.flat_.linkable_ ? view.flat_.file_->rootEntry()
                                                             : nullptr;
            if (!flatRoot || flatRoot->name_ != name)
                ej["name"] = name;
            nlohmann::json overrides = nlohmann::json::array();
            for (const InstanceMember& m : view.members_)
            {
                nlohmann::json ov = diffMember(world, ctx, view, m);
                if (ov.is_null())
                    continue;
                if (const auto cs = ov.find("components"); cs != ov.end())
                    for (const auto& cj : *cs)
                        usedTypes.emplace(cj["type"].get<std::string>());
                overrides.push_back(std::move(ov));
            }
            if (!overrides.empty())
                ej["overrides"] = std::move(overrides);
        }
        else
        {
            ej["name"] = name;
            auto compsJ = reflectedComponents(h, io);
            for (const auto& cj : compsJ)
                usedTypes.emplace(cj["type"].get<std::string>());
            if (auto* uc = reg.try_get<UnknownComponents_C>(en.entity_))
                for (const std::string& blob : uc->blobs_)
                    compsJ.push_back(nlohmann::json::parse(blob));
            ej["components"] = std::move(compsJ);
        }

        entitiesJ.push_back(std::move(ej));
    }

    writeVersions(root, usedTypes);
    return root;
}

struct LoadedFile
{
    std::vector<entt::entity> created_;
    std::unordered_map<uint32_t, size_t> indexById_;
    std::unordered_map<size_t, std::unique_ptr<LoadedFile>> nested_;
};

bool resolveChain(const LoadedFile& file, const IdChain& chain, size_t from, entt::entity& out,
                  const LoadedFile*& level)
{
    if (from >= chain.size())
        return false;
    const auto it = file.indexById_.find(chain[from]);
    if (it == file.indexById_.end() || it->second >= file.created_.size())
        return false;
    if (from + 1 == chain.size())
    {
        out = file.created_[it->second];
        level = &file;
        return true;
    }
    const auto n = file.nested_.find(it->second);
    return n != file.nested_.end() && resolveChain(*n->second, chain, from + 1, out, level);
}

void applyComponentJson(World& world, EntityHandle h, const ComponentType& ct,
                        const nlohmann::json& cj, const FieldIO& io)
{
    void* c = ct.getOrEmplace(*h.reg_, h.entity_);
    if (!c)
        return;
    for (const Field& f : ct.fields)
        if (cj.contains(f.name))
            f.type->fromJson(f.ptrIn(c), cj[f.name], io);
    if (ct.meta.onDeserialized)
        ct.meta.onDeserialized(h, world);
    world.instances().markDirty(h, ct.mask());
}

void applyOverrides(World& world, const Engine& ctx, const LoadedFile& nested,
                    const nlohmann::json& overrides, const std::string& path)
{
    auto& reg = world.registry_;
    for (const auto& ov : overrides)
    {
        if (!ov.is_object())
            continue;
        entt::entity e = entt::null;
        const LoadedFile* level = nullptr;
        if (!resolveChain(nested, overrideChain(ov), 0, e, level) || !reg.valid(e))
        {
            std::cerr << "[Template] " << path
                      << ": an override targets an entity the template no longer has\n";
            continue;
        }
        const EntityHandle h{&reg, e};
        const FieldIO io{ctx, level->created_};
        if (const auto n = ov.find("name"); n != ov.end() && n->is_string())
            if (auto* nc = reg.try_get<Name_C>(e))
                nc->name_ = n->get<std::string>();
        if (const auto cs = ov.find("components"); cs != ov.end() && cs->is_array())
            for (const auto& cj : *cs)
            {
                const ComponentType* ct = ComponentRegistry::instance().find(cj.value("type", ""));
                if (ct && ct->getOrEmplace)
                    applyComponentJson(world, h, *ct, cj, io);
            }
        if (const auto rm = ov.find("removed"); rm != ov.end() && rm->is_array())
            for (const auto& t : *rm)
            {
                const ComponentType* ct =
                    t.is_string() ? ComponentRegistry::instance().find(t.get<std::string>())
                                  : nullptr;
                if (ct && ct->tryGet && ct->remove && ct->tryGet(reg, e))
                {
                    ct->remove(reg, e);
                    world.instances().markDirty(h, ct->mask());
                }
            }
    }
}

void loadFile(World& world, const Engine& ctx, const nlohmann::json& root, bool asMember, int depth,
              LoadedFile& out);

entt::entity loadInstanceEntry(World& world, const Engine& ctx, const nlohmann::json& ej,
                               size_t index, int depth, LoadedFile& out)
{
    auto& reg = world.registry_;
    const std::string rel = normalizeAssetPath(ej["template"].get<std::string>());
    std::string why;
    size_t rootIndex = 0;
    const nlohmann::json* tj =
        depth < kMaxTemplateNesting ? cachedTemplate(templateAbsPath(ctx, rel)) : nullptr;
    if (!tj)
        why = depth < kMaxTemplateNesting ? "cannot read the file" : "templates nested too deep";
    else if (templateRootIndex(*tj, rootIndex, &why))
    {
        auto nested = std::make_unique<LoadedFile>();
        loadFile(world, ctx, *tj, true, depth + 1, *nested);
        const entt::entity rootEntity = nested->created_[rootIndex];
        reg.emplace_or_replace<TemplateInstance_C>(rootEntity, TemplateInstance_C{rel});
        if (const auto n = ej.find("name"); n != ej.end() && n->is_string())
            if (auto* nc = reg.try_get<Name_C>(rootEntity))
                nc->name_ = n->get<std::string>();
        if (const auto ov = ej.find("overrides"); ov != ej.end() && ov->is_array())
            applyOverrides(world, ctx, *nested, *ov, rel);
        out.nested_.emplace(index, std::move(nested));
        return rootEntity;
    }

    std::cerr << "[Template] " << rel << ": " << why << "\n";
    EntityHandle h = world.factory().create(reg, Spawnables[0]);
    if (auto* nc = reg.try_get<Name_C>(h.entity_))
        nc->name_ = ej.value("name", std::filesystem::path(rel).stem().string());
    reg.emplace<TemplateInstance_C>(h.entity_, TemplateInstance_C{rel});
    return h.entity_;
}

void loadFile(World& world, const Engine& ctx, const nlohmann::json& root, bool asMember, int depth,
              LoadedFile& out)
{
    const auto entitiesIt = root.find("entities");
    if (entitiesIt == root.end() || !entitiesIt->is_array())
        return;
    const auto& entitiesJ = *entitiesIt;

    auto& reg = world.registry_;
    auto& factory = world.factory();
    out.created_.reserve(entitiesJ.size());

    // Pass 1 — create every entity first: an EntityRef may name one further
    // down the file.
    for (size_t i = 0; i < entitiesJ.size(); ++i)
    {
        const auto& ej = entitiesJ[i];
        entt::entity e = entt::null;
        if (ej.is_object() && ej.contains("template") && ej["template"].is_string())
            e = loadInstanceEntry(world, ctx, ej, i, depth, out);
        else
        {
            EntityHandle h = factory.create(reg, Spawnables[0]);
            if (auto* nc = reg.try_get<Name_C>(h.entity_))
                nc->name_ = ej.value("name", "");
            e = h.entity_;
        }
        const uint32_t id = entryId(ej, i);
        reg.emplace_or_replace<FileId_C>(e, FileId_C{id});
        out.indexById_.emplace(id, i);
        if (asMember)
            reg.emplace_or_replace<TemplateMember_C>(e);
        else
            reg.remove<TemplateMember_C>(e);
        out.created_.push_back(e);
    }

    const FieldIO io{ctx, out.created_};
    for (size_t i = 0; i < entitiesJ.size(); ++i)
    {
        const auto& ej = entitiesJ[i];
        if (!ej.is_object() || ej.contains("template"))
            continue;
        const auto compsIt = ej.find("components");
        if (compsIt == ej.end() || !compsIt->is_array())
            continue;
        const EntityHandle h{&reg, out.created_[i]};
        for (const auto& cj : *compsIt)
        {
            const ComponentType* ct = ComponentRegistry::instance().find(cj.value("type", ""));
            if (!ct || !ct->getOrEmplace)
            {
                reg.get_or_emplace<UnknownComponents_C>(h.entity_).blobs_.push_back(cj.dump());
                continue;
            }
            applyComponentJson(world, h, *ct, cj, io);
        }
    }

    for (size_t i = 0; i < entitiesJ.size(); ++i)
    {
        const auto& ej = entitiesJ[i];
        if (!ej.is_object())
            continue;
        const auto p = ej.find("parent");
        if (p == ej.end() || p->is_null())
            continue;
        entt::entity parent = entt::null;
        if (p->is_number_integer())
        {
            const int64_t idx = p->get<int64_t>();
            if (idx >= 0 && static_cast<size_t>(idx) < out.created_.size())
                parent = out.created_[static_cast<size_t>(idx)];
        }
        else if (p->is_object())
        {
            const auto inst = p->find("instance");
            const auto at = p->find("at");
            if (inst != p->end() && inst->is_number_unsigned() && at != p->end())
            {
                const auto n = out.nested_.find(inst->get<size_t>());
                const LoadedFile* level = nullptr;
                if (n != out.nested_.end())
                    resolveChain(*n->second, idChain(*at), 0, parent, level);
            }
        }
        if (parent != entt::null && reg.valid(parent))
            Hierarchy_S::attach({&reg, parent}, {&reg, out.created_[i]});
    }
}

Origin originOf(const nlohmann::json& root, const std::vector<entt::entity>& created)
{
    const auto vec3 = [&](const char* key, v3d& out)
    {
        const auto it = root.find(key);
        if (it == root.end() || !it->is_array() || it->size() != 3)
            return false;
        out = {(*it)[0].get<double>(), (*it)[1].get<double>(), (*it)[2].get<double>()};
        return true;
    };
    Origin o;
    vec3("origin", o.pos_);
    if (const auto it = root.find("originAnchor"); it != root.end() && it->is_number_integer())
        if (const auto i = it->get<int64_t>(); i >= 0 && static_cast<size_t>(i) < created.size())
            o.anchor_ = created[static_cast<size_t>(i)];
    // Files from before the offset: the origin was the anchor itself.
    if (!vec3("originOffset", o.offset_) && o.anchor_ == entt::null)
        o.offset_ = o.pos_;
    return o;
}

void clearScene(World& world)
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

void clearSceneAndLoadJson(World& world, const Engine& ctx, const nlohmann::json& root)
{
    clearScene(world);
    LoadedFile loaded;
    loadFile(world, ctx, root, false, 0, loaded);
    world.time_.simTime_ = root.value("time", 0.0);
    world.origin_ = originOf(root, loaded.created_);
}

bool parseFile(const std::string& path, nlohmann::json& out)
{
    std::ifstream f(path);
    if (!f.is_open())
        return false;
    try
    {
        out = nlohmann::json::parse(f);
    }
    catch (...)
    {
        return false;
    }
    return true;
}
}  // namespace

void EntitySerializer::save(World& world, const Engine& ctx, const std::string& path)
{
    std::ofstream f(path);
    f << sceneToJson(world, ctx, true).dump(2);
}

bool EntitySerializer::saveTemplate(World& world, const Engine& ctx, const std::string& path)
{
    std::ofstream f(path);
    if (!f.is_open())
        return false;
    f << sceneToJson(world, ctx, false).dump(2);
    return static_cast<bool>(f);
}

std::string EntitySerializer::templateToBuffer(World& world, const Engine& ctx)
{
    return sceneToJson(world, ctx, false).dump();
}

void EntitySerializer::save(const std::vector<EntityDesc>& entities, const std::string& path)
{
    nlohmann::json root;
    std::unordered_set<std::string> usedTypes;
    auto& entitiesJ = root["entities"] = nlohmann::json::array();

    for (const auto& desc : entities)
    {
        nlohmann::json ej;
        ej["name"] = desc.name;
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

void EntitySerializer::clearSceneAndLoad(World& world, const Engine& ctx, const std::string& path)
{
    nlohmann::json root;
    if (parseFile(path, root))
        clearSceneAndLoadJson(world, ctx, root);
}

std::string EntitySerializer::toBuffer(World& world, const Engine& ctx)
{
    return sceneToJson(world, ctx, true).dump();
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
    clearSceneAndLoadJson(world, ctx, root);
}

std::vector<entt::entity> EntitySerializer::instantiate(World& world, const Engine& ctx,
                                                        const std::string& path)
{
    nlohmann::json root;
    if (!parseFile(path, root))
        return {};
    LoadedFile loaded;
    loadFile(world, ctx, root, false, 0, loaded);
    return loaded.created_;
}

entt::entity EntitySerializer::instantiateLinked(World& world, const Engine& ctx,
                                                 const std::string& absPath, std::string* why)
{
    const std::string rel = templateRelPath(ctx, absPath);
    if (rel.empty())
    {
        *why = "the file is outside the project";
        return entt::null;
    }
    const nlohmann::json* tj = cachedTemplate(templateAbsPath(ctx, rel));
    if (!tj)
    {
        *why = "cannot read the file";
        return entt::null;
    }
    size_t rootIndex = 0;
    if (!templateRootIndex(*tj, rootIndex, why))
        return entt::null;

    const nlohmann::json entry{{"template", rel}, {"parent", nullptr}};
    const nlohmann::json root{{"entities", nlohmann::json::array({entry})}};
    LoadedFile loaded;
    loadFile(world, ctx, root, false, 0, loaded);
    if (loaded.created_.empty())
    {
        *why = "nothing was created";
        return entt::null;
    }
    world.registry_.remove<FileId_C>(loaded.created_[0]);
    return loaded.created_[0];
}
}  // namespace batap
