#include "ScenePanel.h"

#include "Components/Hierarchy_C.h"
#include "Components/Kind_C.h"
#include "Components/Name_C.h"
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

    auto* kindC = reg.try_get<Kind_C>(e);
    const char* icon = spawnableFor(kindC ? kindC->value : EntityKind::Empty).icon;

    auto* hc = reg.try_get<Hierarchy_C>(e);
    bool hasChildren = hc && hc->firstChild != entt::null;
    bool selected = selectedEntity.has_value() && *selectedEntity == h;

    ImGuiTreeNodeFlags flags =
        ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (selected)
        flags |= ImGuiTreeNodeFlags_Selected;

    std::string label = std::string(icon) + " " + reg.get<Name_C>(e).name_;

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
        ImGui::TextUnformatted(reg.get<Name_C>(e).name_.c_str());
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
    auto& reg = world.scene_->registry_;

    if (ImGui::Button(ICON_MD_ADD))
        ImGui::OpenPopup("AddEntityPopup");

    if (ImGui::BeginPopup("AddEntityPopup"))
    {
        for (const Spawnable& s : Spawnables)
        {
            const std::string label = std::string(s.icon) + " " + s.label;
            if (ImGui::MenuItem(label.c_str()))
                world.entityFactory_->create(world.scene_->registry_, s);
        }

        ImGui::EndPopup();
    }

    ImGui::Separator();

    ImGui::Text("Scene");

    // Arbre scrollable — laisse 32px en bas pour la drop zone fixe
    constexpr float kDropZoneH = 32.0f;
    ImGui::BeginChild("##scene_tree", ImVec2(0, -kDropZoneH), false,
                      ImGuiWindowFlags_HorizontalScrollbar);

    for (auto e : reg.storage<entt::entity>())
    {
        auto* hc = reg.try_get<Hierarchy_C>(e);
        if (hc && hc->parent != entt::null)
            continue;
        drawEntityNode(reg, e, selectedEntity);
    }

    ImGui::EndChild();

    // Zone de drop fixe toujours visible en bas → détache l'entité draguée
    ImGui::InvisibleButton("##scenepanel_bg", ImVec2(-1, kDropZoneH));
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY"))
        {
            entt::entity dragged = *static_cast<const entt::entity*>(payload->Data);
            Hierarchy_S::detach({&reg, dragged});
        }
        ImGui::EndDragDropTarget();
    }
}

}  // namespace batap
