#pragma once

#include "Components/EntityHandle.h"
#include "EigenTypes.h"
#include "UI/ThumbnailCache.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace batap
{
struct App;
struct Engine;
struct World;
struct UIPanels;

// An absolute path, null-terminated, of a .btpl or a .bmesh.
inline constexpr const char* kContentPayload = "CONTENT_FILE";

std::vector<EntityHandle> placeContent(World& world, App& app, Engine& ctx,
                                       const std::string& absPath, const std::optional<v3f>& at,
                                       EntityHandle parent, bool link = true);

struct ContentBrowser
{
    void toggle();
    bool open() const { return open_; }
    bool hovered() const { return hovered_; }
    bool grid() const { return grid_; }
    float tileSize() const { return tileSize_; }
    void setView(bool grid, float tileSize);
    void draw(World& world, App& app, Engine& ctx, UIPanels& panels, float x, float width,
              float bottom, float maxHeight);

    struct Entry
    {
        std::string name_;
        std::string rel_;
        std::filesystem::path path_;
        bool dir_ = false;
    };

    struct Folder
    {
        std::string name_;
        std::filesystem::path path_;
        std::vector<Folder> children_;
    };

   private:
    void rescan(const std::string& projectDir);
    void goTo(const std::filesystem::path& folder);
    void refill();
    void drawTree(const Folder& folder, bool isRoot);
    void drawBreadcrumb();
    std::vector<Entry> visible() const;
    void interact(World& world, App& app, Engine& ctx, UIPanels& panels, const Entry& e,
                  bool pressed);
    void drawRows(World& world, App& app, Engine& ctx, UIPanels& panels);
    void drawGrid(World& world, App& app, Engine& ctx, UIPanels& panels);
    void drawCell(World& world, App& app, Engine& ctx, UIPanels& panels, const Entry& e,
                  ImVec2 cell);
    void drawActions(World& world, App& app, Engine& ctx, UIPanels& panels, const Entry& e);
    void activate(World& world, App& app, Engine& ctx, UIPanels& panels, const Entry& e);
    void place(World& world, App& app, Engine& ctx, UIPanels& panels, const Entry& e,
               bool link = true);
    void import(App& app, const Entry& e);

    bool open_ = false;
    bool hovered_ = false;
    bool focusSearch_ = false;
    bool grid_ = true;
    float tileSize_ = 80.0f;
    float height_ = 260.0f;
    std::string scannedProject_;
    Folder root_;
    std::filesystem::path current_;
    std::filesystem::path selected_;
    std::vector<Entry> entries_;
    std::vector<Entry> all_;
    std::string search_;
    ThumbnailCache thumbnails_;
};
}  // namespace batap
