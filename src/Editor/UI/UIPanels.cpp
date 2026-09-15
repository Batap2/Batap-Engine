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
#include "UI/Scoped.h"
#include "UI/UITheme.h"
#include "Systems/Bounds_S.h"
#include "Systems/Physics_S.h"
#include "Systems/Systems.h"
#include "World.h"

#include <imgui.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>

namespace batap
{
namespace
{
constexpr ImGuiWindowFlags kFixedWindowFlags =
    ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus;

// The bar and the rail lay their own content out by hand and have no scrollbar
// to show where the wheel took them.
constexpr ImGuiWindowFlags kNoScrollFlags =
    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

constexpr std::array kSceneFilter{FileDialogFilter{"Scene (.btpl)", "*.btpl"}};
constexpr std::array kAnyAssetFilter{FileDialogFilter{"Assets", "*.*"}};
constexpr std::array kTemplateFilter{FileDialogFilter{"Template (.btpl)", "*.btpl"}};

constexpr float kBarItemGap = 6.0f;
constexpr float kBarTextGap = 10.0f;
constexpr float kBarGroupGap = 16.0f;

constexpr float kPanelMinWidth = 20.0f;
constexpr float kPanelMaxWidth = 600.0f;
constexpr float kResizeGripHalfWidth = 6.0f;
constexpr float kOptionsGripHeight = 11.0f;
constexpr float kOptionsMinHeight = 30.0f;
constexpr float kOptionsMaxHeight = 400.0f;

void drawVerticalEdge(float x)
{
    const ImVec2 p = ImGui::GetWindowPos();
    ImGui::GetWindowDrawList()->AddLine({x, p.y}, {x, p.y + ImGui::GetWindowHeight()},
                                        ImGui::GetColorU32(ui::border));
}

std::string fileStem(const std::string& path)
{
    return std::filesystem::path(path).stem().string();
}
}  // namespace

void UIPanels::selectByName(World& world, std::string_view name)
{
    auto& reg = world.registry_;
    for (auto [e, n] : reg.view<Name_C>().each())
        if (n.name_ == name)
        {
            selection_.add(EntityHandle{&reg, e});
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

    if (!hit.hit())
    {
        if (!input.down(Key::LCtrl))
            clearSelection();
        return;
    }

    const EntityHandle picked{&world.registry_, hit.entity_};
    if (input.down(Key::LCtrl))
        selection_.toggle(picked);
    else
        select(picked);
}

void UIPanels::drawSelectionBounds(World& world, App& app, Engine& ctx)
{
    selection_.prune();

    for (const EntityHandle& ent : selection_.all())
    {
        auto bounds = entityBounds(world, ctx, ent.entity_);
        if (!bounds)
            bounds = app.editorIcons_.boundsOf(world, ent.entity_);
        if (!bounds)
            continue;

        world.debugOverlay().aabb(bounds->min_, bounds->max_, col3{1.f, 0.62f, 0.f});
    }
}

void UIPanels::drawWindowButtons(Engine& ctx, float height)
{
    const float w = kWindowButtonsWidth / 3.0f;
    ImGui::SetCursorPos({ImGui::GetWindowWidth() - kWindowButtonsWidth, 0.0f});

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
    ui::ScopedColor flat{{ImGuiCol_Button, ui::transparent}};

    if (ImGui::Button(ICON_MD_REMOVE "##min", {w, height}))
        platformMinimizeWindow(ctx.nativeWindow());

    ImGui::SameLine(0.0f, 0.0f);
    const bool maximized = platformIsWindowMaximized(ctx.nativeWindow());
    if (ImGui::Button(maximized ? ICON_MD_FILTER_NONE "##max" : ICON_MD_CROP_SQUARE "##max",
                      {w, height}))
        platformToggleMaximizeWindow(ctx.nativeWindow());

    ImGui::SameLine(0.0f, 0.0f);
    {
        ui::ScopedColor danger{{ImGuiCol_ButtonHovered, ui::red}};
        if (ImGui::Button(ICON_MD_CLOSE "##close", {w, height}))
            platformCloseWindow(ctx.nativeWindow());
    }

    ImGui::PopStyleVar();
}

// A plain window, not BeginMainMenuBar: its padding and safe area fight any
// attempt to place things exactly, and there are no menus left in it.
void UIPanels::drawTopBar(App& app, Engine& ctx)
{
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ctx.titleBarHeight_ = kTopBarHeight;

    ImGui::SetNextWindowPos(vp->Pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize({vp->Size.x, kTopBarHeight}, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    if (ImGui::Begin("##TopStrip", nullptr, kFixedWindowFlags | kNoScrollFlags))
    {
        const float side = kRailIconSize;
        const auto centredY = [](float lineHeight) { return (kTopBarHeight - lineHeight) * 0.5f; };

        ImGui::SetCursorPos({kRailInset, kRailInset});
        {
            ui::ScopedColor flat{{ImGuiCol_Button, ui::transparent}};
            // ImageButton pads around the image: unpadded, or the button is
            // wider than the icon and the mark drifts off the rail's centre.
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{0, 0});
            const bool clicked = logo_ ? ImGui::ImageButton("##logo", logo_, {side, side})
                                       : ImGui::Button(ICON_MD_VIEW_IN_AR, {side, side});
            ImGui::PopStyleVar();
            if (clicked)
                railOpen_ = !railOpen_;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(railOpen_ ? "Hide the rail" : "Show the rail");

        float left = kTopBarHeight + kBarItemGap;

        const std::string sceneName =
            currentScenePath_.empty() ? std::string("untitled") : fileStem(currentScenePath_);
        ImGui::SetCursorPos({left, centredY(ImGui::GetTextLineHeight())});
        {
            ui::ScopedColor bright{{ImGuiCol_Text, ui::textBright}};
            ImGui::TextUnformatted(sceneName.c_str());
        }
        left += ImGui::CalcTextSize(sceneName.c_str()).x + kBarTextGap;

        if (!app.projectDir_.empty())
        {
            ui::ScopedFont small{ui::smallFont};
            const std::string project =
                std::filesystem::path(app.projectDir_).filename().string();
            ImGui::SetCursorPos({left, centredY(ImGui::GetTextLineHeight())});
            ImGui::TextDisabled("%s", project.c_str());
        }

        float right = ImGui::GetWindowWidth() - kWindowButtonsWidth - kRailInset;

        if (!app.gameExeName_.empty())
        {
            right -= side;
            ImGui::SetCursorPos({right, kRailInset});
            ui::ScopedColor flat{{ImGuiCol_Button, ui::transparent}};
            if (ui::IconButton(ICON_MD_LAUNCH, side))
                app.runStandalone();
            right -= kBarItemGap;
        }

        right -= side;
        ImGui::SetCursorPos({right, kRailInset});
        {
            ui::ScopedColor play{
                {ImGuiCol_Button, app.playing_ ? ui::orange : ui::cyan},
                {ImGuiCol_ButtonHovered, app.playing_ ? ui::orange : ui::accentHover},
                {ImGuiCol_Text, ui::bg0}};
            if (ui::IconButton(app.playing_ ? ICON_MD_STOP : ICON_MD_PLAY_ARROW, side))
                app.playing_ ? app.stopPlay() : app.startPlay();
        }
        right -= kBarGroupGap;

        {
            const ImGuiIO& io = ImGui::GetIO();
            char stats[32];
            ImFormatString(stats, sizeof(stats), "%d fps   %.2f ms", static_cast<int>(io.Framerate),
                           static_cast<double>(io.DeltaTime) * 1000.0);

            ui::ScopedFont small{ui::smallFont};
            right -= ImGui::CalcTextSize(stats).x;
            ImGui::SetCursorPos({right, centredY(ImGui::GetTextLineHeight())});
            ImGui::TextDisabled("%s", stats);
            right -= kBarGroupGap;

            if (std::chrono::steady_clock::now() < app.toastEnd_)
            {
                right -= ImGui::CalcTextSize(app.toast_.c_str()).x;
                ImGui::SetCursorPos({right, centredY(ImGui::GetTextLineHeight())});
                ImGui::TextUnformatted(app.toast_.c_str());
            }
        }

        const ImVec2 barMin = ImGui::GetWindowPos();
        const float baseline = barMin.y + kTopBarHeight - 1.0f;
        ImGui::GetWindowDrawList()->AddLine({barMin.x, baseline},
                                            {barMin.x + ImGui::GetWindowWidth(), baseline},
                                            ImGui::GetColorU32(ui::border));

        drawWindowButtons(ctx, kTopBarHeight);
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
}

void UIPanels::drawRail(World& world, App& app, Engine& ctx, float top, float height)
{
    if (!railOpen_)
        return;

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos({vp->Pos.x, top}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({kRailWidth, height}, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0, kRailInset});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{0, kRailInset});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ui::ScopedColor flat{{ImGuiCol_Button, ui::transparent}};
    ImGui::Begin("##Rail", nullptr, kFixedWindowFlags | kNoScrollFlags);

    const auto railButton = [](const char* icon, const char* popupId, const char* tooltip)
    {
        ImGui::SetCursorPosX(kRailInset);
        if (ui::IconButton(icon, kRailIconSize))
            ImGui::OpenPopup(popupId);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", tooltip);
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

    const float bottom = ImGui::GetWindowHeight() - kRailIconSize - kRailInset;
    const bool light = app.theme_ == ui::Theme::Light;
    ImGui::SetCursorPos({kRailInset, bottom - kRailIconSize - kRailInset});
    if (ui::IconButton(light ? ICON_MD_DARK_MODE : ICON_MD_LIGHT_MODE, kRailIconSize))
        app.setTheme(light ? ui::Theme::Dark : ui::Theme::Light);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(light ? "Dark theme" : "Light theme");

    ImGui::SetCursorPos({kRailInset, bottom});
    if (ui::IconButton(ICON_MD_CHEVRON_LEFT, kRailIconSize))
        railOpen_ = false;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Hide the rail");

    drawVerticalEdge(ImGui::GetWindowPos().x + ImGui::GetWindowWidth() - 1.0f);

    ImGui::End();
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
        app.openFileDialogAsyncWithAfterJob(
            kSceneFilter,
            [this, &app](std::vector<std::string>&& paths)
            {
                if (paths.empty())
                    return;
                EntitySerializer::clearSceneAndLoad(*app.world_, *app.ctx_, paths[0]);
                currentScenePath_ = paths[0];
                clearSelection();
            });

    if (ImGui::MenuItem("Save Scene..."))
    {
        std::string path = SaveFileDialog(kSceneFilter, ".btpl");
        if (!path.empty())
        {
            EntitySerializer::save(world, *app.ctx_, path);
            app.ctx_->assetManager_->saveAllAssets();
            currentScenePath_ = std::move(path);
        }
    }
}

void UIPanels::drawImportMenu(World& world, App& app, Engine& ctx)
{
    if (ImGui::MenuItem("Import assets"))
        app.openFileDialogAsyncWithAfterJob(kAnyAssetFilter,
                                            [&app](std::vector<std::string>&& paths)
                                            {
                                                for (const auto& path : paths)
                                                    importFile(path, {app.projectDir_});
                                            });

    if (ImGui::MenuItem("Load assets"))
        app.openFileDialogAsyncWithAfterJob(kTemplateFilter,
                                            [&world, &ctx](std::vector<std::string>&& paths)
                                            {
                                                for (const auto& path : paths)
                                                    EntitySerializer::instantiate(world, ctx, path);
                                            });
}

void UIPanels::drawViewMenu(World& world, App& app)
{
    ImGui::MenuItem("Colliders", nullptr, &world.systems().physics_->showColliders_);
    ImGui::MenuItem("Bounds", nullptr, &world.systems().bounds_->showBounds_);
    ImGui::MenuItem("Icons", nullptr, &app.editorIcons_.show_);
}

void UIPanels::drawGizmoOptions()
{
    const float width = ImGui::GetContentRegionAvail().x;
    ImGui::InvisibleButton("##resizeOptions", {width, kOptionsGripHeight});
    if (ImGui::IsItemHovered() || ImGui::IsItemActive())
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    if (ImGui::IsItemActive())
        optionsHeight_ = std::clamp(optionsHeight_ - ImGui::GetIO().MouseDelta.y,
                                    kOptionsMinHeight, kOptionsMaxHeight);

    const ImVec2 windowPos = ImGui::GetWindowPos();
    const float ruleY = std::round(ImGui::GetItemRectMin().y + kOptionsGripHeight * 0.5f);
    ImGui::GetWindowDrawList()->AddLine({windowPos.x, ruleY},
                                        {windowPos.x + ImGui::GetWindowWidth(), ruleY},
                                        ImGui::GetColorU32(ui::border));

    const float side = ImGui::GetFrameHeight() + 6.0f;
    const auto modeButton = [&](const char* icon, GizmoMode mode, const char* tooltip)
    {
        const bool on = gizmo_.mode_ == mode;
        {
            ui::ScopedColor colors{{ImGuiCol_Button, on ? ui::accent : ui::bg2},
                                   {ImGuiCol_ButtonHovered, on ? ui::accentHover : ui::bg3},
                                   {ImGuiCol_Text, on ? ui::bg0 : ui::text}};
            if (ui::IconButton(icon, side))
                gizmo_.mode_ = mode;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", tooltip);
        ImGui::SameLine(0.0f, kBarItemGap);
    };

    modeButton(ICON_MD_OPEN_WITH, GizmoMode::Translate, "Move");
    modeButton(ICON_MD_3D_ROTATION, GizmoMode::Rotate, "Rotate");
    modeButton(ICON_MD_ZOOM_OUT_MAP, GizmoMode::Scale, "Scale");
    ImGui::NewLine();

    ImGui::BeginChild("##gizmoOptionsBody", {0.0f, 0.0f});

    const bool local = gizmo_.local_ || gizmo_.mode_ == GizmoMode::Scale;
    if (ImGui::Button(local ? ICON_MD_VIEW_IN_AR "  Local" : ICON_MD_PUBLIC "  Global",
                      {width, side}))
        gizmo_.local_ = !gizmo_.local_;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Handles follow the entity axes or the world axes."
                          " Scale is always local.");

    const bool center = gizmo_.pivot_ == GizmoPivot::Center;
    if (ImGui::Button(center ? ICON_MD_FILTER_CENTER_FOCUS "  Center"
                             : ICON_MD_ADJUST "  Pivot",
                      {width, side}))
        gizmo_.pivot_ = center ? GizmoPivot::Origin : GizmoPivot::Center;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Place the handles on the transform origin"
                          " or on the centre of the selection bounds.");

    ImGui::Spacing();
    ImGui::TextDisabled("Snap (Ctrl)");
    if (ui::BeginFields fields{"##snapFields"})
    {
        ui::FieldDragFloat("Move", &gizmo_.snapUnits_, 0.05f, 0.0f, 1000.0f);
        ui::FieldDragFloat("Rotate", &gizmo_.snapDegrees_, 0.5f, 0.0f, 180.0f);
        ui::FieldDragFloat("Scale", &gizmo_.snapScale_, 0.01f, 0.0f, 100.0f);
    }

    ImGui::EndChild();
}

void UIPanels::draw(World& world, App& app, Engine& ctx)
{
    const ImGuiViewport* vp = ImGui::GetMainViewport();

    if (!gizmo_.draw(world, ctx, selection_))
        pickOnClick(world, app, ctx);
    drawSelectionBounds(world, app, ctx);
    drawTopBar(app, ctx);

    const float bodyTop = vp->Pos.y + kTopBarHeight;
    const float bodyHeight = vp->Size.y - kTopBarHeight;
    drawRail(world, app, ctx, bodyTop, bodyHeight);
    const float railWidth = railOpen_ ? kRailWidth : 0.0f;

    const auto beginDockedPanel = [&](const char* id, float x, float width)
    {
        ImGui::SetNextWindowPos({x, bodyTop}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({width, bodyHeight}, ImGuiCond_Always);
        ImGui::Begin(id, nullptr, kFixedWindowFlags);
    };

    // sign: which way the width grows when the grip moves right.
    const auto resizeGrip = [&](const char* id, float x, float& width, float sign)
    {
        // The grip spans the panel edge in screen space, outside the layout. Left
        // in it, it stretches the content further down the more the window is
        // scrolled, and the scrolling never reaches an end.
        ImGuiWindow* host = ImGui::GetCurrentWindow();
        const ImVec2 contentMax = host->DC.CursorMaxPos;
        const ImVec2 idealMax = host->DC.IdealMaxPos;

        ImGui::SetCursorScreenPos({x - kResizeGripHalfWidth, vp->Pos.y});
        ImGui::InvisibleButton(id, {kResizeGripHalfWidth * 2.0f, vp->Size.y});
        if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if (ImGui::IsItemActive())
            width = std::clamp(width + ImGui::GetIO().MouseDelta.x * sign, kPanelMinWidth,
                               kPanelMaxWidth);

        host->DC.CursorMaxPos = contentMax;
        host->DC.IdealMaxPos = idealMax;
    };

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{14.0f, 12.0f});

    const float outlinerX = vp->Pos.x + railWidth;
    beginDockedPanel("##Outliner", outlinerX, outlinerWidth_);
    scenePanel_.draw(world, selection_, optionsHeight_ + kOptionsGripHeight);
    drawGizmoOptions();
    resizeGrip("##resizeOutliner", outlinerX + outlinerWidth_, outlinerWidth_, 1.0f);
    drawVerticalEdge(ImGui::GetWindowPos().x + ImGui::GetWindowWidth() - 1.0f);
    ImGui::End();

    const float inspectorX = vp->Pos.x + vp->Size.x - inspectorWidth_;
    beginDockedPanel("##Inspector", inspectorX, inspectorWidth_);
    if (selection_.size() > 1)
    {
        ImGui::TextDisabled("%zu entities selected", selection_.size());
        ImGui::Spacing();
    }
    if (selection_.primary().valid())
        inspectorPanel_.draw(world, app, selection_.primary());

    resizeGrip("##resizeInspector", inspectorX, inspectorWidth_, -1.0f);
    drawVerticalEdge(ImGui::GetWindowPos().x);
    ImGui::End();

    ImGui::PopStyleVar(2);

    materialEditor_.draw(app);
}

void UIPanels::drawStartupScreen(App& app, Engine& ctx)
{
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ctx.titleBarHeight_ = kTopBarHeight;

    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::Begin("##startup", nullptr, kFixedWindowFlags | kNoScrollFlags);

    // The window has no system frame, so it carries the only way to close it.
    drawWindowButtons(ctx, kTopBarHeight);

    const float contentHeight =
        40.0f + static_cast<float>(app.recentProjects_.size()) * 36.0f + 80.0f;
    ImGui::SetCursorPosY((vp->Size.y - contentHeight) * 0.5f);

    const char* title = "Batap Engine";
    ImGui::SetCursorPosX((vp->Size.x - ImGui::CalcTextSize(title).x) * 0.5f);
    ImGui::TextUnformatted(title);
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (!app.recentProjects_.empty())
    {
        ImGui::TextUnformatted("Recent projects:");
        std::string selected;
        for (const auto& dir : app.recentProjects_)
        {
            if (dir.empty())
                continue;
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

    constexpr float kBrowseWidth = 200.0f;
    ImGui::SetCursorPosX((vp->Size.x - kBrowseWidth) * 0.5f);
    if (ImGui::Button("Browse...", {kBrowseWidth, 0}))
        app.openFolderDialogAsyncWithAfterJob(
            [&app](std::vector<std::string>&& paths)
            {
                if (!paths.empty())
                    app.selectProject(paths[0]);
            });

    ImGui::End();
    ImGui::PopStyleVar();
}

}  // namespace batap
