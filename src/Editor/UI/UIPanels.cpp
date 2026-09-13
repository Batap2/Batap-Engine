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
#include "UI/IconsMaterialDesign.h"
#include "Systems/Bounds_S.h"
#include "Systems/Physics_S.h"
#include "Systems/Systems.h"
#include "World.h"

#include <imgui.h>
#include <algorithm>
#include <span>

namespace batap
{

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

void UIPanels::draw(World& world, App& app, Engine& ctx)
{
    ImGuiViewport* vp = ImGui::GetMainViewport();

    pickOnClick(world, app, ctx);
    drawSelectionBounds(world, app, ctx);

    // --- Menu bar ---
    const float menuBarHeight = ImGui::GetFrameHeight();
    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("File"))
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
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Import"))
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
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View"))
        {
            ImGui::MenuItem("Colliders", nullptr, &world.systems().physics_->showColliders_);
            ImGui::MenuItem("Bounds", nullptr, &world.systems().bounds_->showBounds_);
            ImGui::MenuItem("Icons", nullptr, &app.editorIcons_.show_);
            ImGui::EndMenu();
        }

        // Play/Stop (+ Run in its own process)
        {
            const char* label = app.playing_ ? ICON_MD_STOP : ICON_MD_PLAY_ARROW;
            const float w = ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2;
            ImGui::SetCursorPosX((ImGui::GetWindowWidth() - w) * 0.5f);
            if (ImGui::MenuItem(label))
                app.playing_ ? app.stopPlay() : app.startPlay();

            if (!app.gameExeName_.empty() && ImGui::MenuItem(ICON_MD_LAUNCH))
                app.runStandalone();
        }

        if (!app.projectDir_.empty())
        {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.0f);
            ImGui::TextDisabled("%s", app.projectDir_.c_str());
        }

        if (std::chrono::steady_clock::now() < app.toastEnd_)
        {
            const float w = ImGui::CalcTextSize(app.toast_.c_str()).x;
            ImGui::SetCursorPosX(ImGui::GetWindowWidth() - w - 12.0f);
            ImGui::TextUnformatted(app.toast_.c_str());
        }

        ImGui::EndMainMenuBar();
    }

    constexpr float kMinWidth = 20.0f;
    constexpr float kMaxWidth = 600.0f;
    constexpr float kResizeGrip = 6.0f;

    // --- Panneau gauche ---
    ImGui::SetNextWindowPos({vp->Pos.x, vp->Pos.y + menuBarHeight}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({panelWidth_, vp->Size.y - menuBarHeight}, ImGuiCond_Always);
    ImGui::Begin("##LeftPanel", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    float sceneH = selectedEntity_ ? vp->Size.y * 0.45f : 0.0f;
    ImGui::BeginChild("##scene_child", ImVec2(0, sceneH), false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    scenePanel_.draw(world, selectedEntity_);
    ImGui::EndChild();

    if (selectedEntity_)
    {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::BeginChild("##inspectoPanel", ImVec2(0, 0), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoBackground);
        inspectorPanel_.draw(world, app, *selectedEntity_);
        ImGui::EndChild();
    }

    // --- Poignée de redimensionnement (bord droit, entièrement dans la fenêtre) ---
    ImGui::SetCursorScreenPos({vp->Pos.x + panelWidth_ - kResizeGrip * 2.0f, vp->Pos.y});
    ImGui::InvisibleButton("##resize", {kResizeGrip * 2.0f, vp->Size.y});

    if (ImGui::IsItemHovered() || ImGui::IsItemActive())
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

    if (ImGui::IsItemActive())
    {
        panelWidth_ += ImGui::GetIO().MouseDelta.x;
        panelWidth_ = std::clamp(panelWidth_, kMinWidth, kMaxWidth);
    }

    ImGui::End();
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
