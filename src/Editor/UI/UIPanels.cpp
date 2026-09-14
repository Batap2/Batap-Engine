#include "UIPanels.h"

#include "App.h"
#include "Assets/AssetManager.h"
#include "Engine.h"
#include "Importers/FileImporter.h"
#include "InputManager.h"
#include "Renderer/DebugDraw.h"
#include "Serialization/EntitySerializer.h"
#include "Spatial/SpatialIndex.h"
#include "FileDialog.h"
#include "Components/Name_C.h"
#include "UI/IconsMaterialDesign.h"
#include "Platform/PlatformWindow.h"
#include "UI/Field.h"
#include "UI/UITheme.h"
#include "Systems/Bounds_S.h"
#include "Systems/Physics_S.h"
#include "Systems/Systems.h"
#include "World.h"

#include <imgui.h>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <filesystem>
#include <span>

namespace batap
{

void UIPanels::selectByName(World& world, std::string_view name)
{
    auto& reg = world.registry_;
    for (auto [e, n] : reg.view<Name_C>().each())
        if (n.name_ == name)
        {
            select(EntityHandle{&reg, e});
            return;
        }
}

void UIPanels::pickOnClick(World& world, App& app, Engine& ctx)
{
    if (ImGui::GetIO().WantCaptureMouse)
        return;

    InputManager& input = *ctx.inputManager_;
    if (!input.pressed(MouseButton::Left))
        return;

    const Ray ray = rayFromScreen(world, ctx, input.mousePos());
    RayHit hit = world.spatialIndex().raycast(ray);

    // Icons sit on top of what they mark, so they win at equal distance.
    const RayHit icon = app.editorIcons_.raycast(world, ray, hit.hit() ? hit.t_ : ray.tMax_);
    if (icon.hit())
        hit = icon;

    if (hit.hit())
        select(EntityHandle{&world.registry_, hit.entity_});
    else
        clearSelection();
}

void UIPanels::drawSelectionBounds(World& world, App& app, Engine& ctx)
{
    if (!selectedEntity_ || !selectedEntity_->valid())
        return;

    auto bounds = entityBounds(world, ctx, selectedEntity_->entity_);
    if (!bounds)
        bounds = app.editorIcons_.boundsOf(world, selectedEntity_->entity_);
    if (!bounds)
        return;

    world.debugOverlay().aabb(bounds->min_, bounds->max_, col3{1.f, 0.62f, 0.f});
}

void UIPanels::drawWindowButtons(Engine& ctx)
{
    const float h = ImGui::GetWindowHeight();
    const float w = kWindowButtonsWidth / 3.0f;
    ImGui::SetCursorPos({ImGui::GetWindowWidth() - kWindowButtonsWidth, 0.0f});

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0, 0, 0, 0});

    if (ImGui::Button(ICON_MD_REMOVE "##min", {w, h}))
        platformMinimizeWindow(ctx.nativeWindow());

    ImGui::SameLine(0.0f, 0.0f);
    const bool maximized = platformIsWindowMaximized(ctx.nativeWindow());
    if (ImGui::Button(maximized ? ICON_MD_FILTER_NONE "##max" : ICON_MD_CROP_SQUARE "##max",
                      {w, h}))
        platformToggleMaximizeWindow(ctx.nativeWindow());

    ImGui::SameLine(0.0f, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ui::red);
    if (ImGui::Button(ICON_MD_CLOSE "##close", {w, h}))
        platformCloseWindow(ctx.nativeWindow());
    ImGui::PopStyleColor();

    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

void UIPanels::drawRail(World& world, App& app, Engine& ctx, float top, float height)
{
    if (!railOpen_)
        return;

    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos({vp->Pos.x, top}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({kRailWidth, height}, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0, kRailInset});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{0, kRailInset});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0, 0, 0, 0});
    ImGui::Begin("##Rail", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    const float btn = kRailIconSize;
    const float indent = kRailInset;

    auto railButton = [&](const char* icon, const char* popupId, const char* tooltip,
                          bool active = false)
    {
        ImGui::SetCursorPosX(indent);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        if (ui::IconButton(icon, btn))
            ImGui::OpenPopup(popupId);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", tooltip);
        if (active)
            ImGui::GetWindowDrawList()->AddRectFilled({p.x - indent, p.y}, {p.x - indent + 2.0f,
                                                                           p.y + btn},
                                                      ImGui::GetColorU32(ui::cyan));
    };

    railButton(ICON_MD_FOLDER_OPEN, "##railFile", "File");
    if (ImGui::BeginPopup("##railFile"))
    {
        drawFileMenu(world, app);
        ImGui::EndPopup();
    }

    railButton(ICON_MD_FILE_UPLOAD, "##railImport", "Import");
    if (ImGui::BeginPopup("##railImport"))
    {
        drawImportMenu(world, app, ctx);
        ImGui::EndPopup();
    }

    railButton(ICON_MD_VISIBILITY, "##railView", "View");
    if (ImGui::BeginPopup("##railView"))
    {
        drawViewMenu(world, app);
        ImGui::EndPopup();
    }

    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - btn - 8.0f);
    ImGui::SetCursorPosX(indent);
    if (ImGui::Button(ICON_MD_CHEVRON_LEFT, {btn, btn}))
        railOpen_ = false;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Hide the rail");

    {
        const ImVec2 p = ImGui::GetWindowPos();
        const float x = p.x + ImGui::GetWindowWidth() - 1.0f;
        ImGui::GetWindowDrawList()->AddLine({x, p.y}, {x, p.y + ImGui::GetWindowHeight()},
                                            ImGui::GetColorU32(ui::border));
    }

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
}

void UIPanels::drawFileMenu(World& world, App& app)
{
        if (ImGui::MenuItem("Open Project..."))
            app.openFolderDialogAsyncWithAfterJob(
                [&app](std::vector<std::string>&& paths)
                {
                    if (!paths.empty())
                        app.selectProject(paths[0]);
                });

        if (ImGui::MenuItem("Open Scene..."))
        {
            constexpr FileDialogFilter filter{"Scene (.btpl)", "*.btpl"};
            app.openFileDialogAsyncWithAfterJob(
                std::span<const FileDialogFilter>(&filter, 1),
                [this, &app](std::vector<std::string>&& paths)
                {
                    if (paths.empty())
                        return;
                    EntitySerializer::clearSceneAndLoad(*app.world_, *app.ctx_, paths[0]);
                    currentScenePath_ = paths[0];
                    clearSelection();
                });
        }

        if (ImGui::MenuItem("Save Scene..."))
        {
            constexpr FileDialogFilter filter{"Scene (.btpl)", "*.btpl"};
            std::string path = SaveFileDialog(std::span<const FileDialogFilter>(&filter, 1), ".btpl");
            if (!path.empty())
            {
                EntitySerializer::save(world, *app.ctx_, path);
                app.ctx_->assetManager_->saveAllAssets();
                currentScenePath_ = path;
            }
        }
}

void UIPanels::drawImportMenu(World& world, App& app, Engine& ctx)
{
        constexpr FileDialogFilter defaultFilter{"Assets", "*.*"};
        if (ImGui::MenuItem("Import assets"))
            app.openFileDialogAsyncWithAfterJob(
                std::span<const FileDialogFilter>(&defaultFilter, 1),
                [&](std::vector<std::string>&& paths)
                {
                    if (!paths.empty())
                    {
                        for (auto& path : paths)
                        {
                            importFile(path, {app.projectDir_});
                        }
                    }
                });

        constexpr FileDialogFilter bAssetFilter{"Template (.btpl)", "*.btpl"};
        if (ImGui::MenuItem("Load assets"))
            app.openFileDialogAsyncWithAfterJob(
                std::span<const FileDialogFilter>(&bAssetFilter, 1),
                [&](std::vector<std::string>&& paths)
                {
                    if (!paths.empty())
                    {
                        for (auto& path : paths)
                        {
                            EntitySerializer::instantiate(world, ctx, path);
                        }
                    }
                });
}

void UIPanels::drawViewMenu(World& world, App& app)
{
        ImGui::MenuItem("Colliders", nullptr, &world.systems().physics_->showColliders_);
        ImGui::MenuItem("Bounds", nullptr, &world.systems().bounds_->showBounds_);
        ImGui::MenuItem("Icons", nullptr, &app.editorIcons_.show_);
}

void UIPanels::draw(World& world, App& app, Engine& ctx)
{
    ImGuiViewport* vp = ImGui::GetMainViewport();

    pickOnClick(world, app, ctx);
    drawSelectionBounds(world, app, ctx);

    // A plain window, not BeginMainMenuBar: its padding and safe area fight
    // any attempt to place things exactly, and there are no menus left in it.
    const float menuBarHeight = kRailWidth;
    ctx.titleBarHeight_ = menuBarHeight;
    ImGui::SetNextWindowPos(vp->Pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize({vp->Size.x, menuBarHeight}, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    if (ImGui::Begin("##TopStrip", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                         ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                         ImGuiWindowFlags_NoBringToFrontOnFocus))
    {
        const float barH = menuBarHeight;
        const float inset = kRailInset;
        const float side = kRailIconSize;
        const auto textY = [&](float lineH) { return (barH - lineH) * 0.5f; };

        ImGui::SetCursorPos({inset, inset});
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0, 0, 0, 0});
        // ImageButton pads around the image: unpadded, or the button is wider
        // than the icon and the mark drifts off the rail's centre line.
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{0, 0});
        const bool logoClicked = logo_ ? ImGui::ImageButton("##logo", logo_, {side, side})
                                       : ImGui::Button(ICON_MD_VIEW_IN_AR, {side, side});
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        if (logoClicked)
            railOpen_ = !railOpen_;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(railOpen_ ? "Hide the rail" : "Show the rail");

        float x = barH + 6.0f;

        const std::string sceneName =
            currentScenePath_.empty()
                ? std::string("untitled")
                : std::filesystem::path(currentScenePath_).stem().string();
        ImGui::SetCursorPos({x, textY(ImGui::GetTextLineHeight())});
        ImGui::PushStyleColor(ImGuiCol_Text, ui::textBright);
        ImGui::TextUnformatted(sceneName.c_str());
        ImGui::PopStyleColor();
        x += ImGui::CalcTextSize(sceneName.c_str()).x + 10.0f;

        if (!app.projectDir_.empty())
        {
            ui::PushSmallFont small;
            const std::string project =
                std::filesystem::path(app.projectDir_).filename().string();
            ImGui::SetCursorPos({x, textY(ImGui::GetTextLineHeight())});
            ImGui::TextDisabled("%s", project.c_str());
        }

        float right = ImGui::GetWindowWidth() - kWindowButtonsWidth - inset;

        if (!app.gameExeName_.empty())
        {
            right -= side;
            ImGui::SetCursorPos({right, inset});
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0, 0, 0, 0});
            if (ui::IconButton(ICON_MD_LAUNCH, side))
                app.runStandalone();
            ImGui::PopStyleColor();
            right -= 6.0f;
        }

        right -= side;
        ImGui::SetCursorPos({right, inset});
        ImGui::PushStyleColor(ImGuiCol_Button, app.playing_ ? ui::orange : ui::cyan);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                              app.playing_ ? ui::orange : ui::accentHover);
        ImGui::PushStyleColor(ImGuiCol_Text, ui::bg0);
        if (ui::IconButton(app.playing_ ? ICON_MD_STOP : ICON_MD_PLAY_ARROW, side))
            app.playing_ ? app.stopPlay() : app.startPlay();
        ImGui::PopStyleColor(3);
        right -= 16.0f;

        {
            const ImGuiIO& io = ImGui::GetIO();
            std::ostringstream statsOut;
            statsOut << static_cast<int>(io.Framerate) << " fps   " << std::fixed
                     << std::setprecision(2) << io.DeltaTime * 1000.0f << " ms";
            const std::string stats = statsOut.str();

            ui::PushSmallFont small;
            right -= ImGui::CalcTextSize(stats.c_str()).x;
            ImGui::SetCursorPos({right, textY(ImGui::GetTextLineHeight())});
            ImGui::TextDisabled("%s", stats.c_str());
            right -= 16.0f;

            if (std::chrono::steady_clock::now() < app.toastEnd_)
            {
                right -= ImGui::CalcTextSize(app.toast_.c_str()).x;
                ImGui::SetCursorPos({right, textY(ImGui::GetTextLineHeight())});
                ImGui::TextUnformatted(app.toast_.c_str());
            }
        }

        const ImVec2 barMin = ImGui::GetWindowPos();
        ImGui::GetWindowDrawList()->AddLine({barMin.x, barMin.y + barH - 1.0f},
                                            {barMin.x + ImGui::GetWindowWidth(),
                                             barMin.y + barH - 1.0f},
                                            ImGui::GetColorU32(ui::border));

        drawWindowButtons(ctx);
    }
    ImGui::End();
    ImGui::PopStyleVar(2);

    constexpr float kMinWidth = 20.0f;
    constexpr float kMaxWidth = 600.0f;
    constexpr float kResizeGrip = 6.0f;

    const float bodyTop = vp->Pos.y + menuBarHeight;
    const float bodyHeight = vp->Size.y - menuBarHeight;
    drawRail(world, app, ctx, bodyTop, bodyHeight);
    const float railWidth = railOpen_ ? kRailWidth : 0.0f;

    const auto dockedPanel = [&](const char* id, float x, float width)
    {
        ImGui::SetNextWindowPos({x, bodyTop}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({width, bodyHeight}, ImGuiCond_Always);
        ImGui::Begin(id, nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                         ImGuiWindowFlags_NoBringToFrontOnFocus |
                         ImGuiWindowFlags_NoScrollWithMouse);
    };

    const auto panelEdge = [&](bool onLeft)
    {
        const ImVec2 p = ImGui::GetWindowPos();
        const float x = onLeft ? p.x : p.x + ImGui::GetWindowWidth() - 1.0f;
        ImGui::GetWindowDrawList()->AddLine({x, p.y}, {x, p.y + ImGui::GetWindowHeight()},
                                            ImGui::GetColorU32(ui::border));
    };

    // sign: which way the width grows when the grip moves right.
    const auto resizeGrip = [&](const char* id, float x, float& width, float sign)
    {
        ImGui::SetCursorScreenPos({x - kResizeGrip, vp->Pos.y});
        ImGui::InvisibleButton(id, {kResizeGrip * 2.0f, vp->Size.y});
        if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if (ImGui::IsItemActive())
            width = std::clamp(width + ImGui::GetIO().MouseDelta.x * sign, kMinWidth, kMaxWidth);
    };

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{14.0f, 12.0f});

    dockedPanel("##Outliner", vp->Pos.x + railWidth, outlinerWidth_);
    scenePanel_.draw(world, selectedEntity_);
    resizeGrip("##resizeOutliner", vp->Pos.x + railWidth + outlinerWidth_, outlinerWidth_, 1.0f);
    panelEdge(false);
    ImGui::End();

    const float inspectorX = vp->Pos.x + vp->Size.x - inspectorWidth_;
    dockedPanel("##Inspector", inspectorX, inspectorWidth_);
    if (selectedEntity_ && selectedEntity_->valid())
        inspectorPanel_.draw(world, app, *selectedEntity_);
    resizeGrip("##resizeInspector", inspectorX, inspectorWidth_, -1.0f);
    panelEdge(true);
    ImGui::End();

    ImGui::PopStyleVar(2);

    materialEditor_.draw(app);
}

void UIPanels::drawStartupScreen(App& app, Engine& /*ctx*/)
{
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::Begin("##startup", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);

    float contentH = 40.0f + static_cast<float>(app.recentProjects_.size()) * 36.0f + 80.0f;
    ImGui::SetCursorPosY((vp->Size.y - contentH) * 0.5f);

    const char* title = "Batap Engine";
    ImGui::SetCursorPosX((vp->Size.x - ImGui::CalcTextSize(title).x) * 0.5f);
    ImGui::Text("%s", title);
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (!app.recentProjects_.empty())
    {
        ImGui::Text("Recent projects:");
        std::string selected;
        for (const auto& dir : app.recentProjects_)
        {
            if (dir.empty()) continue;
            if (ImGui::Selectable(dir.c_str()))
            {
                selected = dir;
                break;
            }
        }
        if (!selected.empty())
            app.selectProject(selected);
        ImGui::Spacing();
    }

    constexpr float kBtnW = 200.0f;
    ImGui::SetCursorPosX((vp->Size.x - kBtnW) * 0.5f);
    if (ImGui::Button("Browse...", {kBtnW, 0}))
        app.openFolderDialogAsyncWithAfterJob(
            [&app](std::vector<std::string>&& paths)
            {
                if (!paths.empty())
                    app.selectProject(paths[0]);
            });

    ImGui::End();
}

}  // namespace batap
