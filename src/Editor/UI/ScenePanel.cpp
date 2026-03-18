#include "ScenePanel.h"

#include "Components/Camera_C.h"
#include "Components/Hierarchy_C.h"
#include "Components/Mesh_C.h"
#include "Components/Name_C.h"
#include "Components/PointLight_C.h"
#include "Instance/EntityFactory.h"
#include "Scene.h"
#include "Systems/Hierarchy_S.h"
#include "UI/IconsMaterialDesign.h"
#include "World.h"

#include "imgui.h"

namespace batap
{

void ScenePanel::drawEntityNode(entt::registry& reg, entt::entity e,
                                std::optional<EntityHandle>& selectedEntity)
{
    EntityHandle h = {&reg, e};

    const char* icon = ICON_MD_CATEGORY;
    if (reg.all_of<Camera_C>(e))
        icon = ICON_MD_VIDEOCAM;
    else if (reg.all_of<Mesh_C>(e))
        icon = ICON_MD_HVAC;
    else if (reg.all_of<PointLight_C>(e))
        icon = ICON_MD_LIGHTBULB;

    auto* hc = reg.try_get<Hierarchy_C>(e);
    bool hasChildren = hc && hc->firstChild != entt::null;
    bool selected = selectedEntity.has_value() && *selectedEntity == h;

    ImGuiTreeNodeFlags flags =
        ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (selected)
        flags |= ImGuiTreeNodeFlags_Selected;

    std::string label = std::string(icon) + " " + reg.get<Name_C>(e)._name;

    void* nodeId = reinterpret_cast<void*>(static_cast<uintptr_t>(entt::to_integral(e)));

    bool opened = false;
    if (hasChildren)
    {
        opened = ImGui::TreeNodeEx(nodeId, flags, "%s", label.c_str());
    }
    else
    {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        ImGui::TreeNodeEx(nodeId, flags, "%s", label.c_str());
    }

    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
        selectedEntity = h;

    // --- drag source ---
    if (ImGui::BeginDragDropSource())
    {
        ImGui::SetDragDropPayload("ENTITY", &e, sizeof(entt::entity));
        ImGui::TextUnformatted(reg.get<Name_C>(e)._name.c_str());
        ImGui::EndDragDropSource();
    }

    // --- drop target : attache le dragged en enfant de ce noeud ---
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY"))
        {
            entt::entity dragged = *static_cast<const entt::entity*>(payload->Data);
            if (dragged != e)
                Hierarchy_S::attach(h, {&reg, dragged});
        }
        ImGui::EndDragDropTarget();
    }

    if (opened)
    {
        // copie la liste des enfants avant de récurser (la liste peut changer via DnD)
        std::vector<entt::entity> childList;
        for (entt::entity child : Hierarchy_S::children(h))
            childList.push_back(child);

        for (entt::entity child : childList)
            drawEntityNode(reg, child, selectedEntity);

        ImGui::TreePop();
    }
}

void ScenePanel::draw(World& world, std::optional<EntityHandle>& selectedEntity)
{
    auto& reg = world.scene_->_registry;

    if (ImGui::Button(ICON_MD_ADD))
        ImGui::OpenPopup("AddEntityPopup");

    if (ImGui::BeginPopup("AddEntityPopup"))
    {
        if (ImGui::MenuItem(ICON_MD_HVAC " Static Mesh"))
            world.entityFactory_->createStaticMesh(world.scene_->_registry);

        if (ImGui::MenuItem(ICON_MD_LIGHTBULB " Point Light"))
            world.entityFactory_->createPointLight(world.scene_->_registry);

        if (ImGui::MenuItem(ICON_MD_VIDEOCAM " Camera"))
        {
            // action
        }

        ImGui::EndPopup();
    }

    ImGui::Separator();

    // "Scene" est aussi une drop target pour détacher (remettre à la racine)
    ImGui::Text("Scene");
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY"))
        {
            entt::entity dragged = *static_cast<const entt::entity*>(payload->Data);
            Hierarchy_S::detach({&reg, dragged});
        }
        ImGui::EndDragDropTarget();
    }

    // Affiche seulement les entités racines (sans parent)
    for (auto e : reg.storage<entt::entity>())
    {
        auto* hc = reg.try_get<Hierarchy_C>(e);
        if (hc && hc->parent != entt::null)
            continue;
        drawEntityNode(reg, e, selectedEntity);
    }
}

}  // namespace batap
