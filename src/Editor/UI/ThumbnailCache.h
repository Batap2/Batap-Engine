#pragma once

#include "Renderer/Features/Thumbnails/Thumbnails.h"

#include <imgui.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace batap
{
struct Engine;

enum struct ThumbnailKind : uint8_t
{
    Texture,
    Material,
    Mesh,
    Template
};

struct ThumbnailCache
{
    struct Tile
    {
        ImTextureID texture_ = 0;
        ImVec2 uv0_;
        ImVec2 uv1_;
    };

    void beginFrame();
    void invalidate();
    std::optional<Tile> get(Engine& ctx, const std::filesystem::path& abs, const std::string& rel,
                            ThumbnailKind kind);

   private:
    struct Entry
    {
        Thumbnails::Id id_ = Thumbnails::None;
        Thumbnails::Id next_ = Thumbnails::None;
        ThumbnailKind kind_ = ThumbnailKind::Texture;
        uint64_t signature_ = 0;
        uint64_t lastUsed_ = 0;
        bool failed_ = false;
        bool stale_ = false;
        std::vector<std::pair<ThumbnailKind, std::string>> deps_;
    };

    enum struct Outcome
    {
        Queued,
        Failed,
        NoRoom
    };

    Outcome request(Engine& ctx, Entry& entry, const std::filesystem::path& abs,
                    const std::string& rel);
    uint64_t signatureOf(Engine& ctx, const Entry& entry, const std::filesystem::path& abs,
                         const std::string& rel) const;
    bool makeRoom(Thumbnails& thumbs);
    void release(Thumbnails& thumbs, Thumbnails::Id& id);

    std::unordered_map<std::string, Entry> entries_;
    uint64_t frame_ = 0;
    int budget_ = 0;
    uint32_t liveTiles_ = 0;
    bool recheck_ = false;
    bool forceRecheck_ = false;
    std::chrono::steady_clock::time_point recheckAt_{};
};
}  // namespace batap
