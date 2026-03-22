#include "UIPanels.h"

#include "App.h"
#include "Context.h"
#include "Serialization/EntitySerializer.h"
#include "WindowsUtils/FileDialog.h"
#include "World.h"

#include <imgui.h>
#include <algorithm>

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
                            app.projectDir_ = std::move(paths[0]);
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

    constexpr float kMinWidth   = 20.0f;
    constexpr float kMaxWidth   = 600.0f;
    constexpr float kResizeGrip = 6.0f;

    // --- Panneau gauche ---
    ImGui::SetNextWindowPos({vp->Pos.x, vp->Pos.y + menuBarHeight}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({panelWidth_, vp->Size.y - menuBarHeight}, ImGuiCond_Always);
    ImGui::Begin("##LeftPanel", nullptr,
                 ImGuiWindowFlags_NoTitleBar          | ImGuiWindowFlags_NoResize        |
                 ImGuiWindowFlags_NoMove              | ImGuiWindowFlags_NoCollapse      |
                 ImGuiWindowFlags_NoBringToFrontOnFocus |
                 ImGuiWindowFlags_NoScrollbar         | ImGuiWindowFlags_NoScrollWithMouse);

    if (ImGui::Button("Import assets", ImVec2(-1, 0)))
        OpenFilesDialogAsync({}, &app.fileDialogMsgBus_);

    float halfW = (panelWidth_ - ImGui::GetStyle().ItemSpacing.x - kResizeGrip * 2.0f) * 0.5f;
    if (ImGui::Button("Save Scene", ImVec2(halfW, 0)))
    {
        constexpr FileDialogFilter kFilter{"Scene (*.json)", "*.json"};
        std::string path = SaveFileDialog(std::span<const FileDialogFilter>(&kFilter, 1), "json");
        if (!path.empty())
        {
            currentScenePath_ = path;
            EntitySerializer::save(world, ctx, path);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Load Scene", ImVec2(halfW, 0)))
    {
        constexpr FileDialogFilter kFilter{"Scene (*.json)", "*.json"};
        auto paths = OpenFilesDialog(std::span<const FileDialogFilter>(&kFilter, 1));
        if (!paths.empty())
        {
            currentScenePath_ = paths[0];
            selectedEntity_ = std::nullopt;
            EntitySerializer::load(world, ctx, paths[0]);
        }
    }

    ImGui::Spacing();
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
        
        ImGui::BeginChild("##inspectoPanel", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
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
        panelWidth_  = std::clamp(panelWidth_, kMinWidth, kMaxWidth);
    }

    ImGui::End();
}

}  // namespace batap
