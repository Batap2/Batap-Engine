#include "UIPanels.h"

#include "Context.h"
#include "WindowsUtils/FileDialog.h"

#include "imgui.h"

#include <algorithm>

using namespace ImGui;

namespace rayvox
{

UIPanels::UIPanels(Context& ctx) : _ctx(ctx) {}

void UIPanels::Draw()
{
    DrawLeftPanel();
}

void UIPanels::DrawLeftPanel()
{
    ImGuiViewport* vp = ImGui::GetMainViewport();

    static float panelWidth = 300.0f;
    const float minWidth = 200.0f;
    const float maxWidth = 600.0f;
    const float resizeGrip = 6.0f;

    // --- fenêtre host fullscreen invisible ---
    ImGui::SetNextWindowPos(vp->Pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(vp->Size, ImGuiCond_Always);

    ImGuiWindowFlags hostFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                 ImGuiWindowFlags_NoBackground;

    ImGui::Begin("##LeftPanelHost", nullptr, hostFlags);

    // --- panel gauche ---
    ImGui::SetCursorScreenPos(vp->Pos);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.f, 0.f, 0.f, 0.70f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 10));

    ImGui::BeginChild("##LeftPanel", ImVec2(panelWidth, vp->Size.y), false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    if (ImGui::Button("Debug Open Mesh"))
    {
        OpenFilesDialogAsync({}, _ctx._fileDialogMsgBus.get());
    }
    ImGui::Separator();

    ImGui::EndChild();

    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    ImVec2 gripPos(panelWidth - resizeGrip, 0);
    ImGui::SetCursorPos(gripPos);

    if(vp->Size.y != 0.0f){
        ImGui::InvisibleButton("##LeftPanelResize", ImVec2(resizeGrip * 2.0f, vp->Size.y));
    }

    if (ImGui::IsItemHovered() || ImGui::IsItemActive())
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

    if (ImGui::IsItemActive())
    {
        panelWidth += ImGui::GetIO().MouseDelta.x;
        panelWidth = std::clamp(panelWidth, minWidth, maxWidth);
    }

    ImGui::End();
}

}  // namespace rayvox