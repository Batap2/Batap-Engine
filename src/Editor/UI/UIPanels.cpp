#include "UIPanels.h"

#include "App.h"
#include "Assets/AssetManager.h"
#include "Context.h"
#include "Importers/FileImporter.h"
#include "Serialization/EntitySerializer.h"
#include "WindowsUtils/FileDialog.h"
#include "World.h"

#include <imgui.h>
#include <algorithm>
#include <span>

namespace batap
{

void UIPanels::draw(World& world, App& app, Context& ctx)
{
    ImGuiViewport* vp = ImGui::GetMainViewport();

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
                        {
                            app.projectDir_ = std::move(paths[0]);
                            app.ctx_->_assetManager->setBaseDir(app.projectDir_);
                        }
                    });

            if (ImGui::MenuItem("Save Scene..."))
            {
                constexpr FileDialogFilter filter{"Scene (.btpl)", "*.btpl"};
                std::string path = SaveFileDialog(std::span<const FileDialogFilter>(&filter, 1), ".btpl");
                if (!path.empty())
                {
                    EntitySerializer::save(world, *app.ctx_, path);
                    app.ctx_->_assetManager->saveAllAssets();
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

        if (!app.projectDir_.empty())
        {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.0f);
            ImGui::TextDisabled("%s", app.projectDir_.c_str());
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

void UIPanels::drawStartupScreen(App& app, Context& /*ctx*/)
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
