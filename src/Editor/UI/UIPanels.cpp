#include "UIPanels.h"

#include "App.h"
#include "CollapsingGroup.h"
#include "WindowsUtils/FileDialog.h"
#include "World.h"

#include <imgui.h>
#include <algorithm>

namespace batap
{

void UIPanels::draw(World& world, App& app)
{
    ImGuiViewport* vp = ImGui::GetMainViewport();

    constexpr float kMinWidth   = 20.0f;
    constexpr float kMaxWidth   = 600.0f;
    constexpr float kResizeGrip = 6.0f;

    // --- Panneau gauche ---
    ImGui::SetNextWindowPos(vp->Pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize({panelWidth_, vp->Size.y}, ImGuiCond_Always);
    ImGui::Begin("##LeftPanel", nullptr,
                 ImGuiWindowFlags_NoTitleBar          | ImGuiWindowFlags_NoResize        |
                 ImGuiWindowFlags_NoMove              | ImGuiWindowFlags_NoCollapse      |
                 ImGuiWindowFlags_NoBringToFrontOnFocus |
                 ImGuiWindowFlags_NoScrollbar         | ImGuiWindowFlags_NoScrollWithMouse);

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
