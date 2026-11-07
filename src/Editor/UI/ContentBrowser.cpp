#include "UI/ContentBrowser.h"

#include "App.h"
#include "Assets/AssetLoader.h"
#include "Audio/Audio.h"
#include "Components/AudioSource_C.h"
#include "Components/Materials_C.h"
#include "Components/Mesh_C.h"
#include "Components/Name_C.h"
#include "Components/Transform_C.h"
#include "Engine.h"
#include "Serialization/EntitySerializer.h"
#include "Systems/Hierarchy_S.h"
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
#include <cfloat>
#include <cmath>
#include <optional>
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
constexpr float kMinTile = 48.0f;
constexpr float kMaxTile = static_cast<float>(Thumbnails::TileSize);
constexpr float kCellPad = 6.0f;
constexpr float kSizeSliderWidth = 90.0f;
constexpr ImGuiWindowFlags kWindowFlags =
    ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

constexpr std::array<std::string_view, 5> kSkippedDirs = {"build", "out", "bin", "engine",
                                                          "__pycache__"};

enum struct FileKind
{
    Folder,
    Template,
    Mesh,
    Material,
    Texture,
    Hdri,
    Sound,
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

bool isSound(const std::string& ext)
{
    return ext == ".wav" || ext == ".mp3" || ext == ".flac" || ext == ".ogg";
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
    if (isSound(ext))
        return FileKind::Sound;
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
        case FileKind::Sound:
            return {ui::assetIcon(AssetType::Sound), ui::colorOf(ui::assetColor(AssetType::Sound))};
        case FileKind::Source:
            return {ICON_MD_DOWNLOAD, ui::textDim};
        case FileKind::Other:
            break;
    }
    return {ICON_MD_INSERT_DRIVE_FILE, ui::textDim};
}

std::optional<ThumbnailKind> thumbnailKindOf(FileKind kind)
{
    switch (kind)
    {
        case FileKind::Texture:
        case FileKind::Hdri:
            return ThumbnailKind::Texture;
        case FileKind::Material:
            return ThumbnailKind::Material;
        case FileKind::Mesh:
            return ThumbnailKind::Mesh;
        case FileKind::Template:
            return ThumbnailKind::Template;
        case FileKind::Folder:
        case FileKind::Sound:
        case FileKind::Source:
        case FileKind::Other:
            break;
    }
    return std::nullopt;
}

bool skipped(const fs::path& dir)
{
    const std::string name = dir.filename().string();
    if (name.starts_with('.'))
        return true;
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

std::vector<EntityHandle> placeContent(World& world, App& app, Engine& ctx,
                                       const std::string& absPath, const std::optional<v3f>& at,
                                       EntityHandle parent, bool link)
{
    auto& reg = world.registry_;
    std::vector<EntityHandle> roots;
    const std::string ext = lower(fs::path(absPath).extension().string());
    if (ext == ".btpl")
    {
        std::error_code ec;
        if (app.editingTemplate() && fs::equivalent(app.templatePath_, absPath, ec))
        {
            app.showToast("A template cannot contain itself");
            return roots;
        }
        std::string why;
        const entt::entity linked =
            link ? EntitySerializer::instantiateLinked(world, ctx, absPath, &why) : entt::null;
        if (linked != entt::null)
            roots.push_back({&reg, linked});
        else
        {
            if (link)
                app.showToast("Placed as a copy: " + why);
            for (entt::entity e : EntitySerializer::instantiate(world, ctx, absPath))
                if (reg.valid(e) && !Hierarchy_S::hasParent({&reg, e}))
                    roots.push_back({&reg, e});
        }
    }
    else if (ext == ".bmesh")
    {
        std::error_code ec;
        const std::string rel = fs::relative(absPath, app.projectDir_, ec).generic_string();
        const MeshHandle mesh = loadAsset<Mesh>(rel, ctx);
        if (!mesh)
        {
            app.showToast("Could not load " + rel);
            return roots;
        }
        EntityHandle h = world.spawn("mesh");
        if (!h.valid())
            return roots;
        h.get<Name_C>().name_ = fs::path(absPath).stem().string();
        h.write<Mesh_C>().mesh_ = mesh;
        h.emplace<Materials_C>();
        roots.push_back(h);
    }
    else if (isSound(ext))
    {
        std::error_code ec;
        const std::string rel = fs::relative(absPath, app.projectDir_, ec).generic_string();
        const SoundHandle sound = loadAsset<Sound>(rel, ctx);
        if (!sound)
        {
            app.showToast("Could not load " + rel);
            return roots;
        }
        EntityHandle h = world.spawn("audioSource");
        if (!h.valid())
            return roots;
        h.get<Name_C>().name_ = fs::path(absPath).stem().string();
        h.write<AudioSource_C>().sound_ = sound;
        roots.push_back(h);
    }

    for (EntityHandle& h : roots)
    {
        // Fresh from the file, so the world matrix is not built yet: a root's
        // local position is its world one.
        if (at)
            if (const auto* tc = h.try_get<Transform_C>())
                h.setLocalPosition(tc->pos() + *at);
        if (parent.valid())
            h.setParent(parent);
    }
    return roots;
}

void ContentBrowser::toggle()
{
    open_ = !open_;
    if (open_)
    {
        focusSearch_ = true;
        scannedProject_.clear();
    }
}

void ContentBrowser::setView(bool grid, float tileSize)
{
    grid_ = grid;
    tileSize_ = std::clamp(tileSize, kMinTile, kMaxTile);
}

void ContentBrowser::rescan(const std::string& projectDir)
{
    thumbnails_.invalidate();
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

void ContentBrowser::place(World& world, App& app, Engine& ctx, UIPanels& panels, const Entry& e,
                           bool link)
{
    const auto roots = placeContent(world, app, ctx, e.path_.string(), std::nullopt, {}, link);
    if (roots.empty())
        return;
    panels.clearSelection();
    for (const EntityHandle& h : roots)
        panels.addToSelection(h);
}

void ContentBrowser::import(App& app, const Entry& e)
{
    const std::string path = e.path_.string();
    app.importAssets(std::span<const std::string>(&path, 1));
    scannedProject_.clear();
}

void ContentBrowser::drawNewTemplate(App& app, const fs::path& folder)
{
    if (!ImGui::BeginMenu("New template"))
        return;
    if (ImGui::IsWindowAppearing())
    {
        newName_ = "new_template";
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::SetNextItemWidth(160.0f);
    constexpr ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue |
                                          ImGuiInputTextFlags_AutoSelectAll;
    bool create = ImGui::InputText("##name", &newName_, flags);
    ImGui::SameLine();
    create |= ImGui::Button("Create");
    if (create && createTemplate(app, folder))
        ImGui::CloseCurrentPopup();
    ImGui::EndMenu();
}

bool ContentBrowser::createTemplate(App& app, const fs::path& folder)
{
    std::string name = newName_;
    const size_t first = name.find_first_not_of(' ');
    name = first == std::string::npos ? std::string{}
                                      : name.substr(first, name.find_last_not_of(' ') - first + 1);
    if (lower(name).ends_with(".btpl"))
        name.resize(name.size() - 5);
    const bool invalid = std::any_of(name.begin(), name.end(),
                                     [](char c)
                                     {
                                         const auto u = static_cast<unsigned char>(c);
                                         return u < 0x20 || u >= 0x7F ||
                                                std::string_view{"<>:\"/\\|?*"}.find(c) !=
                                                    std::string_view::npos;
                                     });
    if (name.empty() || invalid)
    {
        app.showToast("Invalid template name");
        return false;
    }

    const fs::path path = folder / (name + ".btpl");
    std::error_code ec;
    if (fs::exists(path, ec))
    {
        app.showToast(name + ".btpl already exists");
        return false;
    }
    EntitySerializer::save(std::vector<EntityDesc>{}, path.string());
    if (!fs::exists(path, ec))
    {
        app.showToast("Could not write " + name + ".btpl");
        return false;
    }

    all_.push_back(entryOf(path, root_.path_, false));
    std::sort(all_.begin(), all_.end(), lessByName);
    if (folder != current_)
        goTo(folder);
    else
        refill();
    selected_ = path;
    return true;
}

void ContentBrowser::togglePreview(App& app, Engine& ctx, const Entry& e)
{
    const bool wasPlaying = ctx.audio_->playing(preview_);
    ctx.audio_->stop(preview_);
    preview_ = {};
    if (wasPlaying)
        return;
    if (const SoundHandle sound = loadAsset<Sound>(e.rel_, ctx))
        preview_ = ctx.audio_->play(sound, {.bus = AudioBus::Ui});
    else
        app.showToast("Could not load " + e.rel_);
}

void ContentBrowser::activate(World& world, App& app, Engine& ctx, UIPanels& panels, const Entry& e)
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
            place(world, app, ctx, panels, e);
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
        case FileKind::Sound:
            togglePreview(app, ctx, e);
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
            drawNewTemplate(app, e.path_);
            break;
        case FileKind::Template:
            if (ImGui::MenuItem("Edit template"))
                panels.requestTemplate(app, e.path_.string());
            if (ImGui::MenuItem("Add to scene"))
                place(world, app, ctx, panels, e, true);
            if (ImGui::MenuItem("Add as copy"))
                place(world, app, ctx, panels, e, false);
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
                place(world, app, ctx, panels, e);
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
        case FileKind::Sound:
            if (ImGui::MenuItem(ctx.audio_->playing(preview_) ? "Stop" : "Play"))
                togglePreview(app, ctx, e);
            if (ImGui::MenuItem("Add to scene"))
                place(world, app, ctx, panels, e);
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

std::vector<ContentBrowser::Entry> ContentBrowser::visible() const
{
    const bool searching = !search_.empty();
    const std::string needle = lower(search_);

    // The rows are copied first: an action may rescan while they are walked.
    std::vector<Entry> rows;
    for (const Entry& e : searching ? all_ : entries_)
        if (!searching || lower(e.rel_).find(needle) != std::string::npos)
            rows.push_back(e);
    return rows;
}

void ContentBrowser::interact(World& world, App& app, Engine& ctx, UIPanels& panels, const Entry& e,
                              bool pressed)
{
    const FileKind kind = kindOf(e);
    if (pressed)
    {
        selected_ = e.path_;
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            activate(world, app, ctx, panels, e);
    }
    if ((kind == FileKind::Template || kind == FileKind::Mesh || kind == FileKind::Sound) &&
        ImGui::BeginDragDropSource())
    {
        const Look look = lookOf(kind);
        const std::string abs = e.path_.string();
        ImGui::SetDragDropPayload(kContentPayload, abs.c_str(), abs.size() + 1);
        ImGui::TextColored(look.color_, "%s", look.icon_);
        ImGui::SameLine();
        ImGui::TextUnformatted(e.name_.c_str());
        ImGui::EndDragDropSource();
    }
    if (ImGui::BeginPopupContextItem("##actions"))
    {
        selected_ = e.path_;
        drawActions(world, app, ctx, panels, e);
        ImGui::EndPopup();
    }
}

void ContentBrowser::drawRows(World& world, App& app, Engine& ctx, UIPanels& panels)
{
    const bool searching = !search_.empty();
    const float spaceW = ImGui::CalcTextSize(" ").x;
    const std::vector<Entry> rows = visible();

    for (const Entry& e : rows)
    {
        const Look look = lookOf(kindOf(e));
        const float iconW = ImGui::CalcTextSize(look.icon_).x + 6.0f;
        const std::string pad(static_cast<size_t>(std::ceil(iconW / spaceW)), ' ');
        const std::string label = pad + (searching ? e.rel_ : e.name_);
        const ImVec2 start = ImGui::GetCursorScreenPos();

        ImGui::PushID(e.rel_.c_str());
        const bool pressed = ImGui::Selectable(label.c_str(), e.path_ == selected_,
                                               ImGuiSelectableFlags_AllowDoubleClick);
        ImGui::GetWindowDrawList()->AddText(start, ImGui::GetColorU32(look.color_), look.icon_);
        interact(world, app, ctx, panels, e, pressed);
        ImGui::PopID();
    }

    if (rows.empty())
        ImGui::TextDisabled(searching ? "No match" : "Empty folder");
}

void ContentBrowser::drawCell(World& world, App& app, Engine& ctx, UIPanels& panels, const Entry& e,
                              ImVec2 cell)
{
    const FileKind kind = kindOf(e);
    const Look look = lookOf(kind);
    ImGui::PushID(e.rel_.c_str());
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::Selectable("##cell", e.path_ == selected_,
                                           ImGuiSelectableFlags_AllowDoubleClick, cell);
    const bool showPath = ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) &&
                          !ImGui::IsDragDropActive();
    interact(world, app, ctx, panels, e, pressed);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 tileMin{min.x + (cell.x - tileSize_) * 0.5f, min.y + kCellPad};
    const ImVec2 tileMax{tileMin.x + tileSize_, tileMin.y + tileSize_};
    std::optional<ThumbnailCache::Tile> tile;
    if (const auto thumbnail = thumbnailKindOf(kind))
        tile = thumbnails_.get(ctx, e.path_, e.rel_, *thumbnail);
    if (tile)
    {
        dl->AddRectFilled(tileMin, tileMax, ImGui::GetColorU32(ui::bg2), ui::FrameRounding);
        dl->AddImage(tile->texture_, tileMin, tileMax, tile->uv0_, tile->uv1_);
        dl->AddRectFilled({tileMin.x, tileMax.y - 2.0f}, tileMax, ImGui::GetColorU32(look.color_));
    }
    else
    {
        ImFont* font = ImGui::GetFont();
        const float size = tileSize_ * 0.6f;
        const ImVec2 iconSize = font->CalcTextSizeA(size, FLT_MAX, 0.0f, look.icon_);
        dl->AddText(font, size,
                    {tileMin.x + (tileSize_ - iconSize.x) * 0.5f,
                     tileMin.y + (tileSize_ - iconSize.y) * 0.5f},
                    ImGui::GetColorU32(look.color_), look.icon_);
    }

    const ImVec2 labelMin{min.x + kCellPad * 0.5f, tileMax.y + kCellPad};
    const ImVec2 labelMax{min.x + cell.x - kCellPad * 0.5f,
                          labelMin.y + ImGui::GetTextLineHeight()};
    const ImVec2 textSize = ImGui::CalcTextSize(e.name_.c_str());
    if (textSize.x <= labelMax.x - labelMin.x)
        dl->AddText({labelMin.x + (labelMax.x - labelMin.x - textSize.x) * 0.5f, labelMin.y},
                    ImGui::GetColorU32(ImGuiCol_Text), e.name_.c_str());
    else
        ImGui::RenderTextEllipsis(dl, labelMin, labelMax, labelMax.x, e.name_.c_str(), nullptr,
                                  &textSize);

    if (showPath)
        ImGui::SetTooltip("%s", e.rel_.c_str());
    ImGui::PopID();
}

void ContentBrowser::drawGrid(World& world, App& app, Engine& ctx, UIPanels& panels)
{
    const std::vector<Entry> items = visible();
    if (items.empty())
    {
        ImGui::TextDisabled(search_.empty() ? "Empty folder" : "No match");
        return;
    }

    const ImVec2 spacing = ImGui::GetStyle().ItemSpacing;
    const ImVec2 cell{tileSize_ + kCellPad * 2.0f,
                      tileSize_ + ImGui::GetTextLineHeight() + kCellPad * 3.0f};
    const float avail = ImGui::GetContentRegionAvail().x;
    const size_t columns =
        static_cast<size_t>(std::max(1.0f, std::floor((avail + spacing.x) / (cell.x + spacing.x))));
    const size_t rows = (items.size() + columns - 1) / columns;

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(rows), cell.y + spacing.y);
    while (clipper.Step())
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
            for (size_t col = 0; col < columns; ++col)
            {
                const size_t index = static_cast<size_t>(row) * columns + col;
                if (index >= items.size())
                    break;
                if (col > 0)
                    ImGui::SameLine(0.0f, spacing.x);
                drawCell(world, app, ctx, panels, items[index], cell);
            }
}

void ContentBrowser::draw(World& world, App& app, Engine& ctx, UIPanels& panels, float x,
                          float width, float bottom, float maxHeight)
{
    hovered_ = false;
    if (!open_)
        return;
    if (scannedProject_ != app.projectDir_)
        rescan(app.projectDir_);
    thumbnails_.beginFrame();
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
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - (side + gap) * 3.0f);
    ImGui::InputTextWithHint("##search", ICON_MD_SEARCH "  Search the project...", &search_);
    {
        ui::ScopedColor flat{{ImGuiCol_Button, ui::transparent}};
        ImGui::SameLine(0.0f, gap);
        if (ui::IconButton(grid_ ? ICON_MD_VIEW_LIST "##view" : ICON_MD_GRID_VIEW "##view", side))
        {
            grid_ = !grid_;
            app.markConfigDirty();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(grid_ ? "List view" : "Grid view");
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
    if (grid_)
    {
        ImGui::SameLine();
        const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), right - kSizeSliderWidth));
        ImGui::SetNextItemWidth(kSizeSliderWidth);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {ImGui::GetStyle().FramePadding.x, 0.0f});
        if (ImGui::SliderFloat("##tileSize", &tileSize_, kMinTile, kMaxTile, ""))
            app.markConfigDirty();
        ImGui::PopStyleVar();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Thumbnail size (Ctrl+wheel)");
    }
    ImGui::Separator();
    if (grid_)
    {
        const ImGuiIO& io = ImGui::GetIO();
        if (io.KeyCtrl && io.MouseWheel != 0.0f &&
            ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
        {
            tileSize_ = std::clamp(tileSize_ + io.MouseWheel * 8.0f, kMinTile, kMaxTile);
            app.markConfigDirty();
        }
        drawGrid(world, app, ctx, panels);
    }
    else
        drawRows(world, app, ctx, panels);
    if (ImGui::BeginPopupContextWindow("##folderActions", ImGuiPopupFlags_MouseButtonRight |
                                                              ImGuiPopupFlags_NoOpenOverItems))
    {
        drawNewTemplate(app, current_);
        ImGui::EndPopup();
    }
    ImGui::EndChild();

    ImGui::EndChild();

    hovered_ = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows |
                                      ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        open_ = false;
    ImGui::End();
}
}  // namespace batap
