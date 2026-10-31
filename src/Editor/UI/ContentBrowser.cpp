#include "UI/ContentBrowser.h"

#include "App.h"
#include "Assets/AssetLoader.h"
#include "Components/Materials_C.h"
#include "Components/Mesh_C.h"
#include "Components/Name_C.h"
#include "Engine.h"
#include "Serialization/EntitySerializer.h"
#include "UI/AssetRow.h"
#include "UI/Field.h"
#include "UI/IconsMaterialDesign.h"
#include "UI/Scoped.h"
#include "UI/UIPanels.h"
#include "UI/UITheme.h"
#include "World.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

namespace batap
{
namespace
{
namespace fs = std::filesystem;

constexpr float kGripHeight = 7.0f;
constexpr float kMinHeight = 120.0f;
constexpr float kTreeWidth = 200.0f;
constexpr ImGuiWindowFlags kWindowFlags =
    ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

constexpr std::array<std::string_view, 8> kSkippedDirs = {"build", "out",    ".git",       ".vs",
                                                          ".cache", "engine", "__pycache__", "bin"};

enum struct FileKind
{
    Folder,
    Template,
    Mesh,
    Material,
    Texture,
    Hdri,
    Source,
    Other
};

struct Look
{
    const char* icon_;
    ImVec4 color_;
};

std::string lower(std::string s)
{
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

FileKind kindOf(const ContentBrowser::Entry& e)
{
    if (e.dir_)
        return FileKind::Folder;
    const std::string ext = lower(e.path_.extension().string());
    if (ext == ".btpl")
        return FileKind::Template;
    if (ext == ".bmesh")
        return FileKind::Mesh;
    if (ext == ".bmat")
        return FileKind::Material;
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".btex")
        return FileKind::Texture;
    if (ext == ".hdr")
        return FileKind::Hdri;
    if (ext == ".glb" || ext == ".gltf" || ext == ".fbx" || ext == ".obj" || ext == ".blend")
        return FileKind::Source;
    return FileKind::Other;
}

Look lookOf(FileKind kind)
{
    switch (kind)
    {
        case FileKind::Folder:
            return {ICON_MD_FOLDER, ui::text};
        case FileKind::Template:
            return {ICON_MD_WIDGETS, ui::accent};
        case FileKind::Mesh:
            return {ui::assetIcon(AssetType::Mesh), ui::colorOf(ui::assetColor(AssetType::Mesh))};
        case FileKind::Material:
            return {ui::assetIcon(AssetType::Material),
                    ui::colorOf(ui::assetColor(AssetType::Material))};
        case FileKind::Texture:
            return {ui::assetIcon(AssetType::Texture),
                    ui::colorOf(ui::assetColor(AssetType::Texture))};
        case FileKind::Hdri:
            return {ICON_MD_PANORAMA, ui::cyan};
        case FileKind::Source:
            return {ICON_MD_DOWNLOAD, ui::textDim};
        case FileKind::Other:
            break;
    }
    return {ICON_MD_INSERT_DRIVE_FILE, ui::textDim};
}

bool skipped(const fs::path& dir)
{
    const std::string name = dir.filename().string();
    return std::find(kSkippedDirs.begin(), kSkippedDirs.end(), name) != kSkippedDirs.end();
}

bool lessByName(const ContentBrowser::Entry& a, const ContentBrowser::Entry& b)
{
    if (a.dir_ != b.dir_)
        return a.dir_;
    return lower(a.rel_) < lower(b.rel_);
}

ContentBrowser::Entry entryOf(const fs::path& path, const fs::path& root, bool dir)
{
    std::error_code ec;
    const fs::path rel = fs::relative(path, root, ec);
    return {path.filename().string(), (ec ? path : rel).generic_string(), path, dir};
}
}  // namespace

void ContentBrowser::toggle()
{
    open_ = !open_;
    if (open_)
    {
        focusSearch_ = true;
        scannedProject_.clear();
    }
}

void ContentBrowser::rescan(const std::string& projectDir)
{
    const bool sameProject = scannedProject_ == projectDir;
    scannedProject_ = projectDir;
    root_ = {};
    all_.clear();
    if (projectDir.empty())
        return;

    root_.path_ = fs::path(projectDir);
    root_.name_ = root_.path_.filename().string();

    const auto fill = [&](const auto& self, Folder& folder) -> void
    {
        std::error_code ec;
        for (const auto& item : fs::directory_iterator(folder.path_, ec))
        {
            if (item.is_directory(ec))
            {
                if (skipped(item.path()))
                    continue;
                Folder child;
                child.name_ = item.path().filename().string();
                child.path_ = item.path();
                self(self, child);
                folder.children_.push_back(std::move(child));
            }
            else if (item.is_regular_file(ec))
                all_.push_back(entryOf(item.path(), root_.path_, false));
        }
        std::sort(folder.children_.begin(), folder.children_.end(),
                  [](const Folder& a, const Folder& b) { return lower(a.name_) < lower(b.name_); });
    };
    fill(fill, root_);
    std::sort(all_.begin(), all_.end(), lessByName);

    std::error_code ec;
    if (!sameProject || current_.empty() || !fs::is_directory(current_, ec))
        current_ = root_.path_;
    refill();
}

void ContentBrowser::goTo(const fs::path& folder)
{
    current_ = folder;
    search_.clear();
    refill();
}

void ContentBrowser::refill()
{
    entries_.clear();
    std::error_code ec;
    for (const auto& item : fs::directory_iterator(current_, ec))
    {
        if (item.is_directory(ec))
        {
            if (!skipped(item.path()))
                entries_.push_back(entryOf(item.path(), root_.path_, true));
        }
        else if (item.is_regular_file(ec))
            entries_.push_back(entryOf(item.path(), root_.path_, false));
    }
    std::sort(entries_.begin(), entries_.end(), lessByName);
}

void ContentBrowser::drawTree(const Folder& folder, bool isRoot)
{
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (folder.children_.empty())
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (isRoot)
        flags |= ImGuiTreeNodeFlags_DefaultOpen;
    if (folder.path_ == current_)
        flags |= ImGuiTreeNodeFlags_Selected;

    const std::string label = std::string(ICON_MD_FOLDER) + "  " + folder.name_;
    const bool opened =
        ImGui::TreeNodeEx(folder.path_.string().c_str(), flags, "%s", label.c_str());
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
        goTo(folder.path_);

    if (!opened || folder.children_.empty())
        return;
    for (const Folder& child : folder.children_)
        drawTree(child, false);
    ImGui::TreePop();
}

void ContentBrowser::drawBreadcrumb()
{
    std::vector<fs::path> chain;
    for (fs::path p = current_; !p.empty(); p = p.parent_path())
    {
        chain.push_back(p);
        if (p == root_.path_ || p == p.parent_path())
            break;
    }

    ui::ScopedColor flat{{ImGuiCol_Button, ui::transparent}};
    for (size_t i = chain.size(); i-- > 0;)
    {
        const fs::path& p = chain[i];
        if (i + 1 < chain.size())
        {
            ImGui::SameLine(0.0f, 2.0f);
            ImGui::TextDisabled(ICON_MD_CHEVRON_RIGHT);
            ImGui::SameLine(0.0f, 2.0f);
        }
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::SmallButton(p.filename().string().c_str()))
            goTo(p);
        ImGui::PopID();
    }
}

void ContentBrowser::addMesh(World& world, App& app, UIPanels& panels, const Entry& e)
{
    const MeshHandle mesh = loadAsset<Mesh>(e.rel_, *app.ctx_);
    if (!mesh)
    {
        app.showToast("Could not load " + e.rel_);
        return;
    }
    EntityHandle h = world.spawn("mesh");
    if (!h.valid())
        return;
    h.get<Name_C>().name_ = e.path_.stem().string();
    h.write<Mesh_C>().mesh_ = mesh;
    h.emplace<Materials_C>();
    panels.select(h);
}

void ContentBrowser::import(App& app, const Entry& e)
{
    const std::string path = e.path_.string();
    app.importAssets(std::span<const std::string>(&path, 1));
    scannedProject_.clear();
}

void ContentBrowser::activate(World& world, App& app, Engine& ctx, UIPanels& panels,
                              const Entry& e)
{
    switch (kindOf(e))
    {
        case FileKind::Folder:
            goTo(e.path_);
            break;
        case FileKind::Template:
            panels.requestTemplate(app, e.path_.string());
            break;
        case FileKind::Mesh:
            addMesh(world, app, panels, e);
            break;
        case FileKind::Material:
            if (const MaterialHandle mat = loadAsset<Material>(e.rel_, ctx))
                panels.openMaterialEditor(mat);
            else
                app.showToast("Could not load " + e.rel_);
            break;
        case FileKind::Texture:
        case FileKind::Source:
            import(app, e);
            break;
        case FileKind::Hdri:
        case FileKind::Other:
            app.revealInExplorer(e.path_.string());
            break;
    }
}

void ContentBrowser::drawActions(World& world, App& app, Engine& ctx, UIPanels& panels,
                                 const Entry& e)
{
    switch (kindOf(e))
    {
        case FileKind::Folder:
            if (ImGui::MenuItem("Open"))
                goTo(e.path_);
            break;
        case FileKind::Template:
            if (ImGui::MenuItem("Edit template"))
                panels.requestTemplate(app, e.path_.string());
            if (ImGui::MenuItem("Add to scene"))
                EntitySerializer::instantiate(world, ctx, e.path_.string());
            ImGui::BeginDisabled(app.editingTemplate());
            if (ImGui::MenuItem("Open as scene"))
            {
                EntitySerializer::clearSceneAndLoad(world, ctx, e.path_.string());
                app.setScenePath(e.path_.string());
                panels.clearSelection();
            }
            ImGui::EndDisabled();
            break;
        case FileKind::Mesh:
            if (ImGui::MenuItem("Add to scene"))
                addMesh(world, app, panels, e);
            break;
        case FileKind::Material:
            if (ImGui::MenuItem("Edit material"))
                activate(world, app, ctx, panels, e);
            break;
        case FileKind::Texture:
        case FileKind::Source:
            if (ImGui::MenuItem("Import"))
                import(app, e);
            break;
        case FileKind::Hdri:
        case FileKind::Other:
            break;
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Copy path"))
        ImGui::SetClipboardText(e.rel_.c_str());
    if (ImGui::MenuItem("Show in Explorer"))
        app.revealInExplorer(e.path_.string());
}

void ContentBrowser::drawRows(World& world, App& app, Engine& ctx, UIPanels& panels)
{
    const bool searching = !search_.empty();
    const std::string needle = lower(search_);
    const float spaceW = ImGui::CalcTextSize(" ").x;

    // The rows are copied first: an action may rescan while they are walked.
    std::vector<Entry> rows;
    for (const Entry& e : searching ? all_ : entries_)
        if (!searching || lower(e.rel_).find(needle) != std::string::npos)
            rows.push_back(e);

    for (const Entry& e : rows)
    {
        const Look look = lookOf(kindOf(e));
        const float iconW = ImGui::CalcTextSize(look.icon_).x + 6.0f;
        const std::string pad(static_cast<size_t>(std::ceil(iconW / spaceW)), ' ');
        const std::string label = pad + (searching ? e.rel_ : e.name_);
        const ImVec2 start = ImGui::GetCursorScreenPos();

        ImGui::PushID(e.rel_.c_str());
        if (ImGui::Selectable(label.c_str(), e.path_ == selected_,
                              ImGuiSelectableFlags_AllowDoubleClick))
        {
            selected_ = e.path_;
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                activate(world, app, ctx, panels, e);
        }
        ImGui::GetWindowDrawList()->AddText(start, ImGui::GetColorU32(look.color_), look.icon_);
        if (ImGui::BeginPopupContextItem("##actions"))
        {
            selected_ = e.path_;
            drawActions(world, app, ctx, panels, e);
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }

    if (rows.empty())
        ImGui::TextDisabled(searching ? "No match" : "Empty folder");
}

void ContentBrowser::draw(World& world, App& app, Engine& ctx, UIPanels& panels, float x,
                          float width, float bottom, float maxHeight)
{
    if (!open_)
        return;
    if (scannedProject_ != app.projectDir_)
        rescan(app.projectDir_);
    height_ = std::clamp(height_, kMinHeight, std::max(kMinHeight, maxHeight));

    ImGui::SetNextWindowPos({x, bottom - height_}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({width, height_}, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0f, 0.0f});
    ImGui::Begin("##ContentBrowser", nullptr, kWindowFlags);
    ImGui::PopStyleVar(2);

    ImGui::InvisibleButton("##grip", {width, kGripHeight});
    if (ImGui::IsItemHovered() || ImGui::IsItemActive())
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    if (ImGui::IsItemActive())
        height_ = std::clamp(height_ - ImGui::GetIO().MouseDelta.y, kMinHeight,
                             std::max(kMinHeight, maxHeight));
    const ImVec2 windowPos = ImGui::GetWindowPos();
    ImGui::GetWindowDrawList()->AddLine(windowPos, {windowPos.x + width, windowPos.y},
                                        ImGui::GetColorU32(ui::border));

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{10.0f, 6.0f});
    ImGui::BeginChild("##body", {0.0f, 0.0f}, ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoBackground);
    ImGui::PopStyleVar();

    const float side = ImGui::GetFrameHeight();
    const float gap = ImGui::GetStyle().ItemInnerSpacing.x;
    if (focusSearch_)
    {
        ImGui::SetKeyboardFocusHere();
        focusSearch_ = false;
    }
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - (side + gap) * 2.0f);
    ImGui::InputTextWithHint("##search", ICON_MD_SEARCH "  Search the project...", &search_);
    {
        ui::ScopedColor flat{{ImGuiCol_Button, ui::transparent}};
        ImGui::SameLine(0.0f, gap);
        if (ui::IconButton(ICON_MD_REFRESH, side))
            scannedProject_.clear();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Rescan the project");
        ImGui::SameLine(0.0f, gap);
        if (ui::IconButton(ICON_MD_CLOSE "##closeBrowser", side))
            open_ = false;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Close (Ctrl+Space)");
    }
    ImGui::Spacing();

    ImGui::BeginChild("##folders", {kTreeWidth, 0.0f}, ImGuiChildFlags_Borders);
    drawTree(root_, true);
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("##files", {0.0f, 0.0f}, ImGuiChildFlags_Borders);
    drawBreadcrumb();
    ImGui::Separator();
    drawRows(world, app, ctx, panels);
    ImGui::EndChild();

    ImGui::EndChild();

    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        open_ = false;
    ImGui::End();
}
}  // namespace batap
