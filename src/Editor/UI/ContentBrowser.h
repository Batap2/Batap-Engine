#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace batap
{
struct App;
struct Engine;
struct World;
struct UIPanels;

struct ContentBrowser
{
    void toggle();
    bool open() const { return open_; }
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
    void drawRows(World& world, App& app, Engine& ctx, UIPanels& panels);
    void drawActions(World& world, App& app, Engine& ctx, UIPanels& panels, const Entry& e);
    void activate(World& world, App& app, Engine& ctx, UIPanels& panels, const Entry& e);
    void addMesh(World& world, App& app, UIPanels& panels, const Entry& e);
    void import(App& app, const Entry& e);

    bool open_ = false;
    bool focusSearch_ = false;
    float height_ = 260.0f;
    std::string scannedProject_;
    Folder root_;
    std::filesystem::path current_;
    std::filesystem::path selected_;
    std::vector<Entry> entries_;
    std::vector<Entry> all_;
    std::string search_;
};
}  // namespace batap
