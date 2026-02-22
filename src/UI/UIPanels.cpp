#include "UIPanels.h"
#include <imgui_internal.h>

#include "Assets/AssetManager.h"
#include "Components/EntityHandle.h"
#include "Components/Mesh_C.h"
#include "Components/Name_C.h"
#include "Components/Transform_C.h"
#include "Context.h"
#include "Instance/EntityFactory.h"
#include "Scene.h"
#include "Systems/Systems.h"
#include "Systems/TransformSystem.h"
#include "UI/IconsMaterialDesign.h"
#include "WindowsUtils/FileDialog.h"

#include "imgui.h"

#include <algorithm>
#include <cstddef>

using namespace ImGui;

namespace batap
{

UIPanels::UIPanels(Context& ctx) : _ctx(ctx) {}

void UIPanels::draw()
{
    drawLeftPanel();
}

void UIPanels::drawLeftPanel()
{
    ImGuiViewport* vp = ImGui::GetMainViewport();

    static float panelWidth = 150.0f;
    const float minWidth = 100.0f;
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

    if (ImGui::Button(ICON_MD_ADD))
    {
        ImGui::OpenPopup("AddEntityPopup");
        // OpenFilesDialogAsync({}, _ctx._fileDialogMsgBus.get());
    }

    Separator();
    drawRegistryTree(_ctx._scene->_registry);

    if(_currentlyClickedSelectedEntity){
        Separator();
        drawEntityMenu();
    }

    if (ImGui::BeginPopup("AddEntityPopup"))
    {
        if (ImGui::MenuItem(ICON_MD_HVAC " Static Mesh"))
        {
            auto h = _ctx._entityFactory->createStaticMesh(_ctx._scene->_registry);
        }

        if (ImGui::MenuItem(ICON_MD_VIDEOCAM " Camera"))
        {
            // action
        }

        ImGui::EndPopup();
    }

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

void UIPanels::drawRegistryTree(entt::registry& reg)
{
    Text("Scene");

    int id = 0;
    for (auto e : reg.storage<entt::entity>())
    {
        PushID(id);

        EntityHandle h = {&reg, e};

        const char* icon = ICON_MD_CATEGORY;

        if (reg.all_of<Camera_C>(e))
            icon = ICON_MD_VIDEOCAM;
        else if (reg.all_of<Mesh_C>(e))
            icon = ICON_MD_HVAC;

        TextUnformatted(icon);
        SameLine();

        bool selected = false;
        if(_currentlyClickedSelectedEntity){
            selected = *_currentlyClickedSelectedEntity == h;
        }

        if(MenuItem(reg.get<Name_C>(e)._name.c_str(), nullptr, selected)){
            if(_currentlyClickedSelectedEntity){
                _currentlyClickedSelectedEntity.reset();
            }
            _currentlyClickedSelectedEntity = h;
        }
        PopID();
        id++;
    }
}

void drawTransformMenu(Context& ctx, EntityHandle ent, Transform_C* t){
    auto pos = t->pos();
    if(DragFloat3("Position: ", pos.data())){
        ctx._systems->_transforms->setLocalPosition(ent, pos);
    }
    
}

void drawMeshMenu(Context& ctx, EntityHandle ent){
    auto* meshC = ent.try_get<Mesh_C>();
    if(!meshC) return;

    
}

void UIPanels::drawEntityMenu(){
    auto& r = _ctx._scene->_registry;
    if(!_currentlyClickedSelectedEntity) return;

    auto ent = *_currentlyClickedSelectedEntity;

    auto* transformC =  ent.try_get<Transform_C>();
    if(transformC){
        drawTransformMenu(_ctx, ent, transformC);
    }
}
}  // namespace batap
