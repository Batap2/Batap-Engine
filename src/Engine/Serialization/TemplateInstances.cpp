#include "TemplateInstances.h"

#include "Assets/AssetLoader.h"
#include "Assets/AssetManager.h"
#include "Components/FileId_C.h"
#include "Components/Hierarchy_C.h"
#include "Components/Name_C.h"
#include "Components/TemplateInstance_C.h"
#include "Components/TemplateMember_C.h"
#include "Engine.h"
#include "Instance/InstanceManager.h"
#include "Reflection/ComponentRegistry.h"
#include "Systems/Hierarchy_S.h"
#include "World.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <unordered_map>

namespace batap
{
namespace
{
struct CachedFile
{
    std::filesystem::file_time_type mtime_;
    nlohmann::json json_;
    bool ok_ = false;
};

std::unordered_map<std::string, CachedFile>& templateCache()
{
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wexit-time-destructors"
    static std::unordered_map<std::string, CachedFile> cache;
#pragma clang diagnostic pop
    return cache;
}

FlatEntry* findFlat(FlatFile& file, const IdChain& chain, size_t from, FlatFile** level)
{
    if (from >= chain.size())
        return nullptr;
    for (FlatEntry& e : file.entries_)
    {
        if (e.id_ != chain[from])
            continue;
        if (from + 1 == chain.size())
        {
            if (level)
                *level = &file;
            return &e;
        }
        return e.nested_ ? findFlat(*e.nested_, chain, from + 1, level) : nullptr;
    }
    return nullptr;
}

void applyOverridesFlat(FlatFile& file, const nlohmann::json& overrides)
{
    for (const auto& ov : overrides)
    {
        if (!ov.is_object())
            continue;
        FlatEntry* fe = findFlat(file, overrideChain(ov), 0, nullptr);
        if (!fe)
            continue;
        if (const auto n = ov.find("name"); n != ov.end() && n->is_string())
            fe->name_ = n->get<std::string>();
        if (const auto cs = ov.find("components"); cs != ov.end() && cs->is_array())
            for (const auto& cj : *cs)
            {
                if (!cj.is_object())
                    continue;
                const std::string type = cj.value("type", "");
                if (type.empty())
                    continue;
                nlohmann::json& dst = fe->components_[type];
                if (!dst.is_object())
                    dst = nlohmann::json::object();
                for (auto it = cj.begin(); it != cj.end(); ++it)
                    if (it.key() != "type")
                        dst[it.key()] = it.value();
            }
        if (const auto rm = ov.find("removed"); rm != ov.end() && rm->is_array())
            for (const auto& t : *rm)
                if (t.is_string())
                    fe->components_.erase(t.get<std::string>());
    }
}

bool flattenInto(const Engine& ctx, const std::string& relPath, FlatFile& out, int depth,
                 std::string* why)
{
    out.path_ = relPath;
    if (depth > kMaxTemplateNesting)
    {
        *why = "templates nested too deep";
        return false;
    }
    const nlohmann::json* file = cachedTemplate(templateAbsPath(ctx, relPath));
    if (!file)
    {
        *why = "cannot read " + relPath;
        return false;
    }
    size_t rootIndex = 0;
    if (!templateRootIndex(*file, rootIndex, why))
        return false;

    const auto& entities = (*file)["entities"];
    out.entries_.reserve(entities.size());
    for (size_t i = 0; i < entities.size(); ++i)
    {
        const auto& ej = entities[i];
        FlatEntry fe;
        fe.id_ = entryId(ej, i);
        fe.hasParent_ = ej.contains("parent") && !ej["parent"].is_null();
        if (const auto t = ej.find("template"); t != ej.end() && t->is_string())
        {
            auto nested = std::make_unique<FlatFile>();
            std::string nestedWhy;
            if (flattenInto(ctx, normalizeAssetPath(t->get<std::string>()), *nested, depth + 1,
                            &nestedWhy))
            {
                if (const auto ov = ej.find("overrides"); ov != ej.end() && ov->is_array())
                    applyOverridesFlat(*nested, *ov);
                const FlatEntry* r = nested->rootEntry();
                fe.name_ = r->name_;
                fe.components_ = r->components_;
                fe.nested_ = std::move(nested);
            }
            else
                std::cerr << "[Template] " << relPath << ": " << nestedWhy << "\n";
            if (const auto n = ej.find("name"); n != ej.end() && n->is_string())
                fe.name_ = n->get<std::string>();
        }
        else
        {
            fe.name_ = ej.value("name", "");
            if (const auto cs = ej.find("components"); cs != ej.end() && cs->is_array())
                for (const auto& cj : *cs)
                {
                    if (!cj.is_object())
                        continue;
                    const std::string type = cj.value("type", "");
                    if (type.empty())
                        continue;
                    nlohmann::json fields = cj;
                    fields.erase("type");
                    fe.components_[type] = std::move(fields);
                }
        }
        out.entries_.push_back(std::move(fe));
    }
    return true;
}

void collectMembers(entt::registry& reg, InstanceView& v, entt::entity e, const IdChain& prefix,
                    bool isRoot)
{
    IdChain chain = prefix;
    if (isRoot)
        chain.push_back(v.flat_.file_->rootEntry()->id_);
    else
    {
        auto* fid = reg.try_get<FileId_C>(e);
        if (!fid)
            return;
        chain.push_back(fid->id_);
    }
    const FlatFile* level = nullptr;
    const FlatEntry* entry = v.flat_.find(chain, &level);
    if (!entry)
        return;
    v.members_.push_back({e, chain, entry, level, level->indexOf(*entry)});

    const IdChain& childPrefix = (!isRoot && reg.all_of<TemplateInstance_C>(e)) ? chain : prefix;
    for (entt::entity c : Hierarchy_S::children({&reg, e}))
        if (reg.all_of<TemplateMember_C>(c))
            collectMembers(reg, v, c, childPrefix, false);
}

const nlohmann::json* baseComponent(const InstanceMember& m, std::string_view type)
{
    const auto it = m.entry_->components_.find(type);
    return it != m.entry_->components_.end() && it->is_object() ? &*it : nullptr;
}

nlohmann::json baseFieldJson(const InstanceMember& m, const ComponentType& t, const Field& f,
                             const FieldIO& io)
{
    if (const nlohmann::json* base = baseComponent(m, t.name))
        if (const auto it = base->find(f.name); it != base->end())
            return *it;
    nlohmann::json def;
    if (t.defaultOf)
        f.type->toJson(f.ptrIn(t.defaultOf()), def, io);
    return def;
}

void finishEdit(World& world, EntityHandle h, const ComponentType& t)
{
    if (t.meta.onDeserialized)
        t.meta.onDeserialized(h, world);
    if (t.patch)
        t.patch(*h.reg_, h.entity_);
    world.instances().markDirty(h, t.mask());
}

nlohmann::json* componentJsonIn(nlohmann::json& components, const std::string& type)
{
    if (!components.is_array())
        components = nlohmann::json::array();
    for (auto& cj : components)
        if (cj.is_object() && cj.value("type", "") == type)
            return &cj;
    components.push_back(nlohmann::json{{"type", type}});
    return &components.back();
}
}  // namespace

const FlatEntry* FlatFile::rootEntry() const
{
    for (const FlatEntry& e : entries_)
        if (!e.hasParent_)
            return &e;
    return nullptr;
}

size_t FlatFile::indexOf(const FlatEntry& entry) const
{
    for (size_t i = 0; i < entries_.size(); ++i)
        if (&entries_[i] == &entry)
            return i;
    return entries_.size();
}

const FlatEntry* FlatTemplate::find(const IdChain& chain, const FlatFile** level) const
{
    FlatFile* lvl = nullptr;
    const FlatEntry* e = findFlat(*file_, chain, 0, &lvl);
    if (level)
        *level = lvl;
    return e;
}

const nlohmann::json* cachedTemplate(const std::string& absPath)
{
    std::error_code ec;
    const auto mtime = std::filesystem::last_write_time(absPath, ec);
    if (ec)
        return nullptr;
    auto& cache = templateCache();
    auto it = cache.find(absPath);
    if (it == cache.end() || it->second.mtime_ != mtime)
    {
        CachedFile f{mtime, {}, false};
        if (std::ifstream in(absPath); in.is_open())
        {
            try
            {
                f.json_ = nlohmann::json::parse(in);
                f.ok_ = f.json_.is_object();
            }
            catch (...)
            {
                f.ok_ = false;
            }
        }
        it = cache.insert_or_assign(absPath, std::move(f)).first;
    }
    return it->second.ok_ ? &it->second.json_ : nullptr;
}

void forgetCachedTemplate(const std::string& absPath)
{
    templateCache().erase(absPath);
}

std::string templateAbsPath(const Engine& ctx, const std::string& relPath)
{
    return (std::filesystem::path(ctx.assetManager_->baseDir()) / relPath)
        .lexically_normal()
        .generic_string();
}

std::string templateRelPath(const Engine& ctx, const std::string& absPath)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path base = fs::weakly_canonical(ctx.assetManager_->baseDir(), ec);
    const fs::path abs = fs::weakly_canonical(absPath, ec);
    const fs::path rel = abs.lexically_relative(base);
    if (rel.empty() || *rel.begin() == "..")
        return {};
    return normalizeAssetPath(rel.generic_string());
}

uint32_t entryId(const nlohmann::json& entry, size_t index)
{
    if (entry.is_object())
        if (const auto it = entry.find("id"); it != entry.end() && it->is_number_unsigned())
            if (const uint64_t id = it->get<uint64_t>(); id > 0 && id <= UINT32_MAX)
                return static_cast<uint32_t>(id);
    return static_cast<uint32_t>(index + 1);
}

IdChain idChain(const nlohmann::json& value)
{
    IdChain chain;
    if (value.is_number_unsigned())
        chain.push_back(value.get<uint32_t>());
    else if (value.is_array())
        for (const auto& v : value)
            if (v.is_number_unsigned())
                chain.push_back(v.get<uint32_t>());
    return chain;
}

IdChain overrideChain(const nlohmann::json& override)
{
    const auto it = override.find("entity");
    return it == override.end() ? IdChain{} : idChain(*it);
}

bool templateRootIndex(const nlohmann::json& file, size_t& out, std::string* why)
{
    const auto it = file.find("entities");
    if (it == file.end() || !it->is_array())
    {
        *why = "no entities";
        return false;
    }
    size_t roots = 0;
    for (size_t i = 0; i < it->size(); ++i)
    {
        const auto& ej = (*it)[i];
        if (!ej.is_object())
            continue;
        const auto p = ej.find("parent");
        if (p == ej.end() || p->is_null())
        {
            ++roots;
            out = i;
        }
    }
    if (roots != 1)
    {
        *why = roots == 0 ? "the template has no root entity"
                          : "the template has several root entities";
        return false;
    }
    if ((*it)[out].contains("template"))
    {
        *why = "the template's root is itself a template instance";
        return false;
    }
    return true;
}

FlatTemplate flattenTemplate(const Engine& ctx, const std::string& relPath)
{
    FlatTemplate t;
    t.linkable_ = flattenInto(ctx, relPath, *t.file_, 0, &t.why_);
    return t;
}

entt::entity instanceRootOf(entt::registry& reg, entt::entity e)
{
    for (entt::entity cur = e; cur != entt::null && reg.valid(cur);)
    {
        if (reg.all_of<TemplateInstance_C>(cur) && !reg.all_of<TemplateMember_C>(cur))
            return cur;
        auto* hc = reg.try_get<Hierarchy_C>(cur);
        cur = hc ? hc->parent : entt::null;
    }
    return entt::null;
}

entt::entity owningInstance(entt::registry& reg, entt::entity e)
{
    if (!reg.valid(e) || !reg.all_of<TemplateMember_C>(e))
        return entt::null;
    auto* hc = reg.try_get<Hierarchy_C>(e);
    return hc ? instanceRootOf(reg, hc->parent) : entt::null;
}

const InstanceMember* InstanceView::member(entt::entity e) const
{
    for (const InstanceMember& m : members_)
        if (m.entity_ == e)
            return &m;
    return nullptr;
}

const InstanceMember* InstanceView::member(const IdChain& chain) const
{
    for (const InstanceMember& m : members_)
        if (m.chain_ == chain)
            return &m;
    return nullptr;
}

std::vector<entt::entity> InstanceView::levelEntities(const FlatFile& level) const
{
    std::vector<entt::entity> out(level.entries_.size(), entt::entity{entt::null});
    for (const InstanceMember& m : members_)
        if (m.level_ == &level && m.entryIndex_ < out.size())
            out[m.entryIndex_] = m.entity_;
    return out;
}

InstanceView viewInstance(World& world, const Engine& ctx, entt::entity root)
{
    auto& reg = world.registry_;
    InstanceView v;
    v.root_ = root;
    auto* ti = reg.valid(root) ? reg.try_get<TemplateInstance_C>(root) : nullptr;
    if (!ti)
    {
        v.flat_.why_ = "not a template instance";
        return v;
    }
    v.path_ = ti->path_;
    v.flat_ = flattenTemplate(ctx, ti->path_);
    if (v.flat_.linkable_)
        collectMembers(reg, v, root, {}, true);
    return v;
}

nlohmann::json diffMember(World& world, const Engine& ctx, const InstanceView& view,
                          const InstanceMember& m)
{
    auto& reg = world.registry_;
    const std::vector<entt::entity> entities = view.levelEntities(*m.level_);
    const FieldIO io{ctx, entities};

    nlohmann::json comps = nlohmann::json::array();
    nlohmann::json removed = nlohmann::json::array();
    for (const ComponentType& t : ComponentRegistry::instance().all())
    {
        if (!t.tryGet)
            continue;
        const void* c = t.tryGet(reg, m.entity_);
        const bool inBase = baseComponent(m, t.name) != nullptr;
        if (!c)
        {
            if (inBase)
                removed.push_back(t.name);
            continue;
        }
        nlohmann::json cj = nlohmann::json::object();
        for (const Field& f : t.fields)
        {
            nlohmann::json now;
            f.type->toJson(f.ptrIn(c), now, io);
            if (now != baseFieldJson(m, t, f, io))
                cj[f.name] = std::move(now);
        }
        if (!inBase || !cj.empty())
        {
            cj["type"] = t.name;
            comps.push_back(std::move(cj));
        }
    }

    nlohmann::json ov = nlohmann::json::object();
    if (!comps.empty())
        ov["components"] = std::move(comps);
    if (!removed.empty())
        ov["removed"] = std::move(removed);
    if (auto* nc = reg.try_get<Name_C>(m.entity_);
        nc && m.entity_ != view.root_ && nc->name_ != m.entry_->name_)
        ov["name"] = nc->name_;
    if (ov.empty())
        return nullptr;
    ov["entity"] = m.chain_;
    return ov;
}

bool overridesField(const nlohmann::json& diff, std::string_view type, std::string_view field)
{
    if (!diff.is_object())
        return false;
    const auto cs = diff.find("components");
    if (cs == diff.end())
        return false;
    for (const auto& cj : *cs)
        if (cj.value("type", "") == type)
            return cj.contains(field);
    return false;
}

bool overridesComponent(const nlohmann::json& diff, std::string_view type)
{
    if (!diff.is_object())
        return false;
    if (const auto cs = diff.find("components"); cs != diff.end())
        for (const auto& cj : *cs)
            if (cj.value("type", "") == type)
                return true;
    if (const auto rm = diff.find("removed"); rm != diff.end())
        for (const auto& t : *rm)
            if (t.is_string() && t.get<std::string>() == type)
                return true;
    return false;
}

void revertField(World& world, const Engine& ctx, const InstanceView& view, const InstanceMember& m,
                 const ComponentType& t, const Field& f)
{
    auto& reg = world.registry_;
    void* c = t.tryGet ? t.tryGet(reg, m.entity_) : nullptr;
    if (!c)
        return;
    const std::vector<entt::entity> entities = view.levelEntities(*m.level_);
    const FieldIO io{ctx, entities};
    f.type->fromJson(f.ptrIn(c), baseFieldJson(m, t, f, io), io);
    finishEdit(world, {&reg, m.entity_}, t);
}

void revertComponent(World& world, const Engine& ctx, const InstanceView& view,
                     const InstanceMember& m, const ComponentType& t)
{
    auto& reg = world.registry_;
    const EntityHandle h{&reg, m.entity_};
    if (!baseComponent(m, t.name))
    {
        if (t.tryGet && t.remove && t.tryGet(reg, m.entity_))
        {
            t.remove(reg, m.entity_);
            world.instances().markDirty(h, t.mask());
        }
        return;
    }
    void* c = t.getOrEmplace ? t.getOrEmplace(reg, m.entity_) : nullptr;
    if (!c)
        return;
    const std::vector<entt::entity> entities = view.levelEntities(*m.level_);
    const FieldIO io{ctx, entities};
    for (const Field& f : t.fields)
        f.type->fromJson(f.ptrIn(c), baseFieldJson(m, t, f, io), io);
    finishEdit(world, h, t);
}

void revertInstance(World& world, const Engine& ctx, const InstanceView& view,
                    bool keepRootTransform)
{
    auto& reg = world.registry_;
    for (const InstanceMember& m : view.members_)
    {
        for (const ComponentType& t : ComponentRegistry::instance().all())
        {
            if (!t.tryGet)
                continue;
            if (keepRootTransform && m.entity_ == view.root_ && t.name == "transform")
                continue;
            if (!baseComponent(m, t.name) && !t.tryGet(reg, m.entity_))
                continue;
            revertComponent(world, ctx, view, m, t);
        }
        if (auto* nc = reg.try_get<Name_C>(m.entity_))
            nc->name_ = m.entry_->name_;
    }
}

int applyToTemplate(World& world, const Engine& ctx, const InstanceView& view,
                    const ApplyFilter& keep, std::string* why)
{
    const std::string abs = templateAbsPath(ctx, view.path_);
    const nlohmann::json* cached = cachedTemplate(abs);
    if (!cached)
    {
        *why = "cannot read " + view.path_;
        return -1;
    }
    nlohmann::json file = *cached;
    nlohmann::json& entities = file["entities"];

    struct Change
    {
        IdChain chain_;
        std::string type_;
        std::string field_;
        nlohmann::json before_;
        nlohmann::json after_;
    };
    std::vector<Change> changes;
    int skipped = 0;
    bool changed = false;

    for (const InstanceMember& m : view.members_)
    {
        const nlohmann::json diff = diffMember(world, ctx, view, m);
        if (diff.is_null())
            continue;
        const bool top = m.chain_.size() == 1 && m.entryIndex_ < entities.size();
        nlohmann::json* target = nullptr;
        if (top)
        {
            nlohmann::json& ej = entities[m.entryIndex_];
            if (m.entry_->nested_)
            {
                nlohmann::json& ovs = ej["overrides"];
                if (!ovs.is_array())
                    ovs = nlohmann::json::array();
                const IdChain rootChain{m.entry_->nested_->rootEntry()->id_};
                for (auto& ov : ovs)
                    if (ov.is_object() && overrideChain(ov) == rootChain)
                        target = &ov;
                if (!target)
                {
                    ovs.push_back(nlohmann::json{{"entity", rootChain}});
                    target = &ovs.back();
                }
            }
            else
                target = &ej;
        }

        if (const auto n = diff.find("name"); n != diff.end() && keep(m, "", "name"))
        {
            if (target)
            {
                (*target)["name"] = *n;
                changed = true;
            }
            else
                ++skipped;
        }
        if (const auto cs = diff.find("components"); cs != diff.end())
        {
            const std::vector<entt::entity> level = view.levelEntities(*m.level_);
            const FieldIO io{ctx, level};
            for (const auto& cj : *cs)
            {
                const std::string type = cj.value("type", "");
                const ComponentType* ct = ComponentRegistry::instance().find(type);
                nlohmann::json* dst = nullptr;
                for (auto it = cj.begin(); it != cj.end(); ++it)
                {
                    if (it.key() == "type" || !keep(m, type, it.key()))
                        continue;
                    if (!target)
                    {
                        ++skipped;
                        continue;
                    }
                    if (!dst)
                        dst = componentJsonIn((*target)["components"], type);
                    Change ch{m.chain_, type, it.key(), nullptr, it.value()};
                    if (ct)
                        for (const Field& f : ct->fields)
                            if (f.name == it.key())
                                ch.before_ = baseFieldJson(m, *ct, f, io);
                    (*dst)[it.key()] = it.value();
                    changes.push_back(std::move(ch));
                    changed = true;
                }
                if (!dst && cj.size() == 1 && target && keep(m, type, ""))
                {
                    componentJsonIn((*target)["components"], type);
                    changed = true;
                }
            }
        }
        if (const auto rm = diff.find("removed"); rm != diff.end())
            for (const auto& t : *rm)
            {
                const std::string type = t.get<std::string>();
                if (!keep(m, type, ""))
                    continue;
                if (!target || m.entry_->nested_)
                {
                    ++skipped;
                    continue;
                }
                nlohmann::json& comps = (*target)["components"];
                if (!comps.is_array())
                    continue;
                for (auto it = comps.begin(); it != comps.end(); ++it)
                    if (it->is_object() && it->value("type", "") == type)
                    {
                        comps.erase(it);
                        changed = true;
                        break;
                    }
            }
    }

    if (!changed)
        return skipped;

    {
        std::ofstream out(abs);
        if (!out.is_open())
        {
            *why = "cannot write " + view.path_;
            return -1;
        }
        out << file.dump(2);
    }
    forgetCachedTemplate(abs);

    auto& reg = world.registry_;
    std::vector<entt::entity> others;
    for (auto [e, ti] : reg.view<TemplateInstance_C>().each())
        if (e != view.root_ && ti.path_ == view.path_)
            others.push_back(e);
    for (entt::entity other : others)
    {
        const InstanceView v2 = viewInstance(world, ctx, other);
        if (!v2.flat_.linkable_)
            continue;
        for (const Change& ch : changes)
        {
            const InstanceMember* m2 = v2.member(ch.chain_);
            const ComponentType* ct = ComponentRegistry::instance().find(ch.type_);
            if (!m2 || !ct || !ct->tryGet)
                continue;
            void* c = ct->tryGet(reg, m2->entity_);
            if (!c)
                continue;
            const std::vector<entt::entity> level = v2.levelEntities(*m2->level_);
            const FieldIO io{ctx, level};
            for (const Field& f : ct->fields)
            {
                if (f.name != ch.field_)
                    continue;
                nlohmann::json now;
                f.type->toJson(f.ptrIn(c), now, io);
                if (now == ch.before_)
                {
                    f.type->fromJson(f.ptrIn(c), ch.after_, io);
                    finishEdit(world, {&reg, m2->entity_}, *ct);
                }
                break;
            }
        }
    }
    return skipped;
}

void unlinkInstance(World& world, entt::entity root)
{
    auto& reg = world.registry_;
    if (!reg.valid(root))
        return;
    reg.remove<TemplateInstance_C>(root);
    reg.remove<TemplateMember_C>(root);
    reg.remove<FileId_C>(root);

    std::vector<entt::entity> stack;
    for (entt::entity c : Hierarchy_S::children({&reg, root}))
        stack.push_back(c);
    while (!stack.empty())
    {
        const entt::entity e = stack.back();
        stack.pop_back();
        if (!reg.all_of<TemplateMember_C>(e))
            continue;
        reg.remove<TemplateMember_C>(e);
        reg.remove<FileId_C>(e);
        if (reg.all_of<TemplateInstance_C>(e))
            continue;
        for (entt::entity c : Hierarchy_S::children({&reg, e}))
            stack.push_back(c);
    }
}
}  // namespace batap
