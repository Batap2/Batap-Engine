#include "UIPanels.h"

#include "App.h"
#include "WindowsUtils/FileDialog.h"
#include "World.h"

#include <imgui.h>
#include <algorithm>

namespace batap
{

void UIPanels::draw(World& world, App& app)
{
    ImGuiViewport* vp = ImGui::GetMainViewport();

    constexpr float kMinWidth   = 100.0f;
    constexpr float kMaxWidth   = 600.0f;
    constexpr float kResizeGrip = 6.0f;

    // Fenêtre host fullscreen invisible (nécessaire pour le child + grip)
    ImGui::SetNextWindowPos(vp->Pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(vp->Size, ImGuiCond_Always);
    ImGui::Begin("##LeftPanelHost", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoCollapse |
                 ImGuiWindowFlags_NoBackground);

    // --- Panneau gauche ---
    ImGui::SetCursorScreenPos(vp->Pos);

    ImGui::BeginChild("##LeftPanel", ImVec2(panelWidth_, vp->Size.y), false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    if (ImGui::Button("Import assets", ImVec2(-1, 0)))
        OpenFilesDialogAsync({}, &app.fileDialogMsgBus_);

    ImGui::Spacing();
    scenePanel_.draw(world, selectedEntity_);

    if (selectedEntity_)
    {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        inspectorPanel_.draw(world, app, *selectedEntity_);
    }

    ImGui::EndChild();

    // --- Poignée de redimensionnement ---
    ImGui::SetCursorPos(ImVec2(panelWidth_ - kResizeGrip, 0));
    if (vp->Size.y > 0.0f)
        ImGui::InvisibleButton("##resize", ImVec2(kResizeGrip * 2.0f, vp->Size.y));

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
