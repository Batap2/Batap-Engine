#include "UIPanels.h"

#include "Assets/AssetManager.h"
#include "Components/Transform_C.h"
#include "Context.h"
#include "Instance/EntityFactory.h"
#include "Scene.h"
#include "WindowsUtils/FileDialog.h"


#include "Systems/Systems.h"
#include "Systems/TransformSystem.h"

#include "imgui.h"

#include <algorithm>
#include <cstddef>

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

    if (ImGui::Button("import mesh"))
    {
        OpenFilesDialogAsync({}, _ctx._fileDialogMsgBus.get());
    }
    SameLine();
    if (ImGui::Button("create mesh entity"))
    {
        for (auto& [handle, _] : _ctx._assetManager->_meshes)
        {
            for (size_t i = 0; i < 100; ++i)
            {
                for (size_t ii = 0; ii < 1; ++ii)
                {
                    auto h = _ctx._entityFactory->createStaticMesh(_ctx._scene->_registry, handle);
                    _ctx._systems->_transforms->translate(
                        h, v3f(static_cast<float>(i) * 2, 0, static_cast<float>(ii) * 2));
                }
            }
        }
    }

    ImGui::Separator();

    ImGui::EndChild();

    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    ImVec2 gripPos(panelWidth - resizeGrip, 0);
    ImGui::SetCursorPos(gripPos);

    if (vp->Size.y != 0.0f)
    {
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
