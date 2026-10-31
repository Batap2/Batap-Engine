#include "UI/ThumbnailCache.h"

#include "Assets/AssetLoader.h"
#include "Assets/AssetManager.h"
#include "Assets/Mesh.h"
#include "Assets/Texture.h"
#include "Engine.h"
#include "Renderer/Renderer.h"
#include "Serialization/BmatSerializer.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <exception>
#include <fstream>
#include <system_error>
#include <unordered_map>

namespace batap
{
namespace
{
namespace fs = std::filesystem;

constexpr int kRequestsPerFrame = 3;
constexpr auto kRecheckPeriod = std::chrono::milliseconds(500);

uint64_t mix(uint64_t h, uint64_t v)
{
    return h ^ (v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2));
}

uint64_t mtimeOf(const fs::path& path)
{
    std::error_code ec;
    const auto time = fs::last_write_time(path, ec);
    return ec ? 0 : static_cast<uint64_t>(time.time_since_epoch().count());
}

uint64_t hashOf(const Material& material)
{
    const auto bytes = std::bit_cast<std::array<std::byte, sizeof(Material)>>(material);
    uint64_t h = 14695981039346656037ull;
    for (const std::byte b : bytes)
    {
        h ^= static_cast<uint64_t>(b);
        h *= 1099511628211ull;
    }
    return h;
}

uint64_t liveHash(AssetManager& assets, const std::string& rel)
{
    const Material* live = assets.get<Material>(rel);
    return live ? hashOf(*live) : 0;
}

std::optional<Material> materialAt(AssetManager& assets, const std::string& rel)
{
    if (const Material* live = assets.get<Material>(rel))
        return *live;
    const auto data = readBmat((fs::path(assets.baseDir()) / rel).string());
    if (!data)
        return std::nullopt;
    const auto texture = [&](const std::string& path)
    {
        if (path.empty())
            return InvalidGPUIndex;
        const TextureHandle handle = loadAsset<Texture>(path, assets);
        const Texture* tex = handle ? assets.get(handle) : nullptr;
        return tex ? tex->bindlessIndex_ : InvalidGPUIndex;
    };
    Material mat = data->mat;
    mat.albedoTexIdx_ = texture(data->albedoTexPath);
    mat.normalTexIdx_ = texture(data->normalTexPath);
    mat.roughnessTexIdx_ = texture(data->roughnessTexPath);
    mat.metallicTexIdx_ = texture(data->metallicTexPath);
    return mat;
}

std::optional<ThumbnailDraw> meshDraw(AssetManager& assets, const std::string& rel,
                                      const m4f& world)
{
    const MeshHandle handle = loadAsset<Mesh>(rel, assets);
    const Mesh* mesh = handle ? assets.get(handle) : nullptr;
    if (!mesh || !mesh->isRenderable())
        return std::nullopt;
    ThumbnailDraw draw;
    draw.mesh_ = *mesh;
    draw.world_ = world;
    draw.materials_.resize(std::max<size_t>(mesh->subMeshCount, 1));
    return draw;
}

struct TemplateNode
{
    m4f local_ = m4f::Identity();
    int parent_ = -1;
    bool visible_ = true;
    std::string mesh_;
    std::vector<std::string> slots_;
};

float component(const nlohmann::json& array, size_t index)
{
    return array[index].get<float>();
}

v3f vec3At(const nlohmann::json& j, const char* key, const v3f& fallback)
{
    const auto it = j.find(key);
    if (it == j.end() || !it->is_array() || it->size() != 3)
        return fallback;
    return {component(*it, 0), component(*it, 1), component(*it, 2)};
}

quatf quatAt(const nlohmann::json& j, const char* key)
{
    const auto it = j.find(key);
    if (it == j.end() || !it->is_array() || it->size() != 4)
        return quatf::Identity();
    const quatf q{component(*it, 3), component(*it, 0), component(*it, 1), component(*it, 2)};
    return q.norm() > 0.f ? q.normalized() : quatf::Identity();
}

std::vector<TemplateNode> readTemplate(const fs::path& abs)
{
    std::ifstream file(abs);
    if (!file.is_open())
        return {};
    try
    {
        const nlohmann::json root = nlohmann::json::parse(file);
        const auto entities = root.find("entities");
        if (entities == root.end() || !entities->is_array())
            return {};
        std::vector<TemplateNode> nodes;
        nodes.reserve(entities->size());
        for (const nlohmann::json& ej : *entities)
        {
            TemplateNode& node = nodes.emplace_back();
            if (const auto parent = ej.find("parent");
                parent != ej.end() && parent->is_number_integer())
                node.parent_ = parent->get<int>();
            const auto components = ej.find("components");
            if (components == ej.end() || !components->is_array())
                continue;
            for (const nlohmann::json& cj : *components)
            {
                const auto type = cj.find("type");
                if (type == cj.end() || !type->is_string())
                    continue;
                const std::string& name = type->get_ref<const std::string&>();
                if (name == "transform")
                    node.local_ = TRS(vec3At(cj, "pos", v3f::Zero()), quatAt(cj, "rot"),
                                      vec3At(cj, "scale", v3f::Ones()));
                else if (name == "mesh")
                {
                    if (const auto mesh = cj.find("mesh"); mesh != cj.end() && mesh->is_string())
                        node.mesh_ = mesh->get<std::string>();
                    if (const auto visible = cj.find("visible");
                        visible != cj.end() && visible->is_boolean())
                        node.visible_ = visible->get<bool>();
                }
                else if (name == "materials")
                {
                    const auto slots = cj.find("slots");
                    if (slots == cj.end() || !slots->is_array())
                        continue;
                    for (const nlohmann::json& slot : *slots)
                        node.slots_.push_back(slot.is_string() ? slot.get<std::string>()
                                                               : std::string{});
                }
            }
        }
        return nodes;
    }
    catch (const std::exception&)
    {
        return {};
    }
}

std::vector<ThumbnailDraw> templateDraws(AssetManager& assets, const fs::path& abs,
                                         std::vector<std::pair<ThumbnailKind, std::string>>& deps)
{
    const std::vector<TemplateNode> nodes = readTemplate(abs);
    std::vector<m4f> worlds(nodes.size(), m4f::Identity());
    std::vector<uint8_t> state(nodes.size(), 0);
    const auto worldOf = [&](const auto& self, size_t i) -> m4f
    {
        if (state[i] == 2)
            return worlds[i];
        if (state[i] == 1)
            return nodes[i].local_;
        state[i] = 1;
        m4f world = nodes[i].local_;
        const int parent = nodes[i].parent_;
        if (parent >= 0 && static_cast<size_t>(parent) < nodes.size())
            world = self(self, static_cast<size_t>(parent)) * world;
        worlds[i] = world;
        state[i] = 2;
        return world;
    };

    std::unordered_map<std::string, Material> materials;
    std::vector<ThumbnailDraw> draws;
    size_t materialCount = 0;
    for (size_t i = 0; i < nodes.size() && draws.size() < Thumbnails::MaxDraws; ++i)
    {
        const TemplateNode& node = nodes[i];
        if (node.mesh_.empty() || !node.visible_)
            continue;
        auto draw = meshDraw(assets, node.mesh_, worldOf(worldOf, i));
        if (!draw)
            continue;
        if (materialCount + draw->materials_.size() > Thumbnails::MaxMaterials)
            break;
        materialCount += draw->materials_.size();
        deps.emplace_back(ThumbnailKind::Mesh, node.mesh_);
        const size_t used = std::min(draw->materials_.size(), node.slots_.size());
        for (size_t s = 0; s < used; ++s)
        {
            const std::string& slot = node.slots_[s];
            if (slot.empty())
                continue;
            auto known = materials.find(slot);
            if (known == materials.end())
            {
                known =
                    materials.emplace(slot, materialAt(assets, slot).value_or(Material{})).first;
                deps.emplace_back(ThumbnailKind::Material, slot);
            }
            draw->materials_[s] = known->second;
        }
        draws.push_back(std::move(*draw));
    }
    return draws;
}
}  // namespace

void ThumbnailCache::beginFrame()
{
    ++frame_;
    budget_ = kRequestsPerFrame;
    const auto now = std::chrono::steady_clock::now();
    recheck_ = forceRecheck_ || now >= recheckAt_;
    if (recheck_)
        recheckAt_ = now + kRecheckPeriod;
    forceRecheck_ = false;
}

void ThumbnailCache::invalidate()
{
    for (auto& [key, entry] : entries_)
        entry.failed_ = false;
    forceRecheck_ = true;
}

void ThumbnailCache::release(Thumbnails& thumbs, Thumbnails::Id& id)
{
    if (id == Thumbnails::None)
        return;
    thumbs.remove(id);
    id = Thumbnails::None;
    --liveTiles_;
}

bool ThumbnailCache::makeRoom(Thumbnails& thumbs)
{
    if (liveTiles_ < Thumbnails::TileCount)
        return true;
    Entry* oldest = nullptr;
    for (auto& [key, entry] : entries_)
    {
        const bool holds = entry.id_ != Thumbnails::None || entry.next_ != Thumbnails::None;
        if (holds && entry.lastUsed_ < frame_ && (!oldest || entry.lastUsed_ < oldest->lastUsed_))
            oldest = &entry;
    }
    if (!oldest)
        return false;
    release(thumbs, oldest->id_);
    release(thumbs, oldest->next_);
    return true;
}

uint64_t ThumbnailCache::signatureOf(Engine& ctx, const Entry& entry, const fs::path& abs,
                                     const std::string& rel) const
{
    AssetManager& assets = *ctx.assetManager_;
    const fs::path base(assets.baseDir());
    uint64_t h = mtimeOf(abs);
    if (entry.kind_ == ThumbnailKind::Material)
        h = mix(h, liveHash(assets, rel));
    for (const auto& [kind, dep] : entry.deps_)
    {
        h = mix(h, mtimeOf(base / dep));
        if (kind == ThumbnailKind::Material)
            h = mix(h, liveHash(assets, dep));
    }
    return h;
}

ThumbnailCache::Outcome ThumbnailCache::request(Engine& ctx, Entry& entry, const fs::path& abs,
                                                const std::string& rel)
{
    AssetManager& assets = *ctx.assetManager_;
    Thumbnails& thumbs = ctx.renderer_->thumbnails();
    entry.deps_.clear();

    Thumbnails::Id id = Thumbnails::None;
    switch (entry.kind_)
    {
        case ThumbnailKind::Texture: {
            const TextureHandle handle = loadAsset<Texture>(rel, assets);
            const Texture* tex = handle ? assets.get(handle) : nullptr;
            if (!tex)
                break;
            if (!makeRoom(thumbs))
                return Outcome::NoRoom;
            id = thumbs.addImage(tex->bindlessIndex_, tex->sizeX_, tex->sizeY_,
                                 tex->format_ == ResourceFormat::R32G32B32A32_FLOAT);
            break;
        }
        case ThumbnailKind::Material: {
            const auto mat = materialAt(assets, rel);
            if (!mat)
                break;
            if (!makeRoom(thumbs))
                return Outcome::NoRoom;
            id = thumbs.addMaterial(*mat);
            break;
        }
        case ThumbnailKind::Mesh: {
            auto draw = meshDraw(assets, rel, m4f::Identity());
            if (!draw)
                break;
            if (!makeRoom(thumbs))
                return Outcome::NoRoom;
            std::vector<ThumbnailDraw> draws;
            draws.push_back(std::move(*draw));
            id = thumbs.addScene(std::move(draws));
            break;
        }
        case ThumbnailKind::Template: {
            std::vector<ThumbnailDraw> draws = templateDraws(assets, abs, entry.deps_);
            if (draws.empty())
                break;
            if (!makeRoom(thumbs))
                return Outcome::NoRoom;
            id = thumbs.addScene(std::move(draws));
            break;
        }
    }

    entry.signature_ = signatureOf(ctx, entry, abs, rel);
    if (id == Thumbnails::None)
        return Outcome::Failed;
    entry.next_ = id;
    ++liveTiles_;
    return Outcome::Queued;
}

std::optional<ThumbnailCache::Tile> ThumbnailCache::get(Engine& ctx, const fs::path& abs,
                                                        const std::string& rel, ThumbnailKind kind)
{
    Thumbnails& thumbs = ctx.renderer_->thumbnails();
    auto [it, inserted] = entries_.try_emplace(abs.generic_string());
    Entry& entry = it->second;
    entry.lastUsed_ = frame_;
    if (inserted)
        entry.kind_ = kind;
    else if (recheck_ && entry.next_ == Thumbnails::None &&
             signatureOf(ctx, entry, abs, rel) != entry.signature_)
    {
        entry.stale_ = true;
        entry.failed_ = false;
    }

    const bool wanted = entry.id_ == Thumbnails::None || entry.stale_;
    if (wanted && entry.next_ == Thumbnails::None && !entry.failed_ && budget_ > 0)
    {
        --budget_;
        const Outcome outcome = request(ctx, entry, abs, rel);
        if (outcome != Outcome::NoRoom)
            entry.stale_ = false;
        if (outcome == Outcome::Failed)
        {
            entry.failed_ = true;
            release(thumbs, entry.id_);
        }
    }

    if (entry.next_ != Thumbnails::None && thumbs.ready(entry.next_))
    {
        release(thumbs, entry.id_);
        entry.id_ = entry.next_;
        entry.next_ = Thumbnails::None;
    }
    if (entry.id_ == Thumbnails::None || !thumbs.ready(entry.id_))
        return std::nullopt;
    return Tile{ctx.renderer_->imguiTexture(thumbs.atlas()), thumbs.uv0(entry.id_),
                thumbs.uv1(entry.id_)};
}
}  // namespace batap
