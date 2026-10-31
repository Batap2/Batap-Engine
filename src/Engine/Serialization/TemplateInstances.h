#pragma once

#include "Components/EntityHandle.h"

#include <entt/entt.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace batap
{
struct World;
struct Engine;
struct ComponentType;
struct Field;

inline constexpr int kMaxTemplateNesting = 8;

using IdChain = std::vector<uint32_t>;

struct FlatFile;

struct FlatEntry
{
    uint32_t id_ = 0;
    std::string name_;
    bool hasParent_ = false;
    nlohmann::json components_ = nlohmann::json::object();
    std::unique_ptr<FlatFile> nested_;
};

struct FlatFile
{
    std::string path_;
    std::vector<FlatEntry> entries_;

    const FlatEntry* rootEntry() const;
    size_t indexOf(const FlatEntry& entry) const;
};

struct FlatTemplate
{
    bool linkable_ = false;
    std::string why_;
    std::unique_ptr<FlatFile> file_ = std::make_unique<FlatFile>();

    const FlatEntry* find(const IdChain& chain, const FlatFile** level = nullptr) const;
};

const nlohmann::json* cachedTemplate(const std::string& absPath);
void forgetCachedTemplate(const std::string& absPath);
std::string templateAbsPath(const Engine& ctx, const std::string& relPath);
std::string templateRelPath(const Engine& ctx, const std::string& absPath);
bool templateRootIndex(const nlohmann::json& file, size_t& out, std::string* why);
uint32_t entryId(const nlohmann::json& entry, size_t index);
IdChain idChain(const nlohmann::json& value);
IdChain overrideChain(const nlohmann::json& override);

FlatTemplate flattenTemplate(const Engine& ctx, const std::string& relPath);

entt::entity instanceRootOf(entt::registry& reg, entt::entity e);
entt::entity owningInstance(entt::registry& reg, entt::entity e);

struct InstanceMember
{
    entt::entity entity_ = entt::null;
    IdChain chain_;
    const FlatEntry* entry_ = nullptr;
    const FlatFile* level_ = nullptr;
    size_t entryIndex_ = 0;
};

struct InstanceView
{
    entt::entity root_ = entt::null;
    std::string path_;
    FlatTemplate flat_;
    std::vector<InstanceMember> members_;

    const InstanceMember* member(entt::entity e) const;
    const InstanceMember* member(const IdChain& chain) const;
    std::vector<entt::entity> levelEntities(const FlatFile& level) const;
};

InstanceView viewInstance(World& world, const Engine& ctx, entt::entity root);

nlohmann::json diffMember(World& world, const Engine& ctx, const InstanceView& view,
                          const InstanceMember& m);
bool overridesField(const nlohmann::json& diff, std::string_view type, std::string_view field);
bool overridesComponent(const nlohmann::json& diff, std::string_view type);

void revertField(World& world, const Engine& ctx, const InstanceView& view, const InstanceMember& m,
                 const ComponentType& t, const Field& f);
void revertComponent(World& world, const Engine& ctx, const InstanceView& view,
                     const InstanceMember& m, const ComponentType& t);
void revertInstance(World& world, const Engine& ctx, const InstanceView& view,
                    bool keepRootTransform);

using ApplyFilter =
    std::function<bool(const InstanceMember& m, std::string_view type, std::string_view field)>;
int applyToTemplate(World& world, const Engine& ctx, const InstanceView& view,
                    const ApplyFilter& keep, std::string* why);

void unlinkInstance(World& world, entt::entity root);
}  // namespace batap
