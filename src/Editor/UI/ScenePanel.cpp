#include "ScenePanel.h"

#include "Components/Hierarchy_C.h"
#include "Components/Name_C.h"
#include "Instance/InstanceManager.h"
#include "Instance/Spawnable.h"
#include "Reflection/ComponentRegistry.h"
#include "Systems/Hierarchy_S.h"
#include "UI/IconsMaterialDesign.h"
#include "Renderer/Renderer.h"
#include "UI/UITheme.h"
#include "World.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "misc/cpp/imgui_stdlib.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <vector>

namespace batap
{

static bool isAncestorOf(const entt::registry& reg, entt::entity e,
                         const std::optional<EntityHandle>& selected)
{
    if (!selected)
        return false;
    entt::entity cur = selected->entity_;
    while (const auto* hc = reg.try_get<Hierarchy_C>(cur))
    {
        if (hc->parent == entt::null)
            break;
        if (hc->parent == e)
            return true;
        cur = hc->parent;
    }
    return false;
}

static void sortByName(entt::registry& reg, std::vector<entt::entity>& entities)
{
    std::sort(entities.begin(), entities.end(),
              [&](entt::entity a, entt::entity b)
              {
                  const std::string& na = reg.get<Name_C>(a).name_;
                  const std::string& nb = reg.get<Name_C>(b).name_;
                  return std::lexicographical_compare(
                      na.begin(), na.end(), nb.begin(), nb.end(), [](char x, char y) {
                          return std::tolower(static_cast<unsigned char>(x)) <
                                 std::tolower(static_cast<unsigned char>(y));
                      });
              });
}

static EntityHandle duplicateEntity(World& world, EntityHandle src)
{
    auto& reg = *src.reg_;
    EntityHandle dst = world.spawn(Spawnables[0]);
    reg.get<Name_C>(dst.entity_).name_ = reg.get<Name_C>(src.entity_).name_;

    for (const ComponentType& t : ComponentRegistry::instance().all())
    {
        if (!t.tryGet || !t.tryGet(reg, src.entity_))
            continue;
        t.copy(reg, src.entity_, dst.entity_);
        if (t.meta.onDeserialized)
            t.meta.onDeserialized(dst, world);
        world.instances().markDirty(dst, t.mask());
    }

    std::vector<entt::entity> childList;
    for (entt::entity child : Hierarchy_S::children(src))
        childList.push_back(child);
    for (entt::entity child : childList)
        Hierarchy_S::attach(dst, duplicateEntity(world, {&reg, child}));

    if (auto* hc = reg.try_get<Hierarchy_C>(src.entity_); hc && hc->parent != entt::null)
        Hierarchy_S::attach({&reg, hc->parent}, dst);

    return dst;
}

void ScenePanel::drawEntityNode(World& world, entt::entity e,
                                std::optional<EntityHandle>& selectedEntity)
{
    auto& reg = world.registry_;
    EntityHandle h = {&reg, e};

    if (renaming_ && *renaming_ == h)
    {
        ImGui::SetNextItemWidth(-1.0f);
        if (renameFocusPending_)
        {
            ImGui::SetKeyboardFocusHere();
            renameFocusPending_ = false;
        }
        ImGui::InputText("##rename", &renameBuffer_);
        if (ImGui::IsItemDeactivated())
        {
            if (!renameBuffer_.empty())
                reg.get<Name_C>(e).name_ = renameBuffer_;
            renaming_.reset();
        }
        return;
    }

    const Spawnable& kind = spawnableFor(reg, e);

    auto* hc = reg.try_get<Hierarchy_C>(e);
    bool hasChildren = hc && hc->firstChild != entt::null;
    bool selected = selectedEntity.has_value() && *selectedEntity == h;

    ImGuiTreeNodeFlags flags =
        ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;

    const bool onSelectedPath = selected || isAncestorOf(reg, e, selectedEntity);
    ImGui::PushStyleColor(ImGuiCol_Text, onSelectedPath ? ui::textBright : ui::text);

    // The spaces hold the glyph's place: it is painted separately, in the
    // kind's colour, and left in the label it would be drawn twice.
    constexpr float kIconGap = 4.0f;
    const float spaceW = ImGui::CalcTextSize(" ").x;
    const float iconW = ImGui::CalcTextSize(kind.icon).x + kIconGap;
    std::string pad(static_cast<size_t>(std::ceil(iconW / spaceW)), ' ');
    std::string label = pad + reg.get<Name_C>(e).name_;

    void* nodeId = reinterpret_cast<void*>(static_cast<uintptr_t>(entt::to_integral(e)));

    const ImVec2 nodeStart = ImGui::GetCursorScreenPos();

    // Before the node, since the fill goes under the text: ImGui's own covers
    // the node rect only and cannot be corrected after. Whole pixels, or
    // antialiased edges make two rows bleed into each other.
    const float optical = std::round(ui::TextOpticalOffsetY());
    const float rowTop = std::round(nodeStart.y) - kRowMargin - optical;
    const float rowBottom = rowTop + ImGui::GetFrameHeight() + kRowMargin * 2.0f;
    const ImRect inner = ImGui::GetCurrentWindow()->InnerRect;
    const bool hovered = ImGui::IsMouseHoveringRect({inner.Min.x, rowTop},
                                                    {inner.Max.x, rowBottom}, false) &&
                         ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);

    if (selected || hovered)
        ImGui::GetWindowDrawList()->AddRectFilled(
            {inner.Min.x, rowTop}, {inner.Max.x, rowBottom},
            ImGui::GetColorU32(selected ? ImGuiCol_Header : ImGuiCol_HeaderHovered));

    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4{0, 0, 0, 0});
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4{0, 0, 0, 0});

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

    ImGui::PopStyleColor(3);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (selected)
        dl->AddRectFilled({ImGui::GetItemRectMin().x, rowTop},
                          {ImGui::GetItemRectMin().x + 2.0f, rowBottom},
                          ImGui::GetColorU32(ui::colorOf(kind.color)));

    // The glyph carries the offset keeping icons on the baseline inside text;
    // drawn on its own it has to come back out.
    dl->AddText({nodeStart.x + ImGui::GetTreeNodeToLabelSpacing(),
                 nodeStart.y + ImGui::GetStyle().FramePadding.y - Renderer::kIconGlyphOffsetY},
                ImGui::GetColorU32(ui::colorOf(kind.color)), kind.icon);

    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
        selectedEntity = h;

    if (ImGui::BeginPopupContextItem())
    {
        selectedEntity = h;
        if (ImGui::MenuItem("Rename"))
        {
            renaming_ = h;
            renameBuffer_ = reg.get<Name_C>(e).name_;
            renameFocusPending_ = true;
        }
        if (ImGui::MenuItem("Duplicate"))
            pendingDuplicate_ = h;
        if (ImGui::MenuItem("Delete"))
            pendingDelete_ = h;
        ImGui::EndPopup();
    }

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
        sortByName(reg, childList);

        for (entt::entity child : childList)
            drawEntityNode(world, child, selectedEntity);

        ImGui::TreePop();
    }
}

void ScenePanel::draw(World& world, std::optional<EntityHandle>& selectedEntity)
{
    auto& reg = world.registry_;

    if (ImGui::BeginPopup("AddEntityPopup"))
    {
        for (const Spawnable& s : Spawnables)
        {
            const std::string label = std::string(s.icon) + " " + s.label;
            if (ImGui::MenuItem(label.c_str()))
                world.spawn(s);
        }

        ImGui::EndPopup();
    }

    constexpr float kDropZoneH = 32.0f;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{1.0f, ImGui::GetStyle().FramePadding.y});
    ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 14.0f);
    // A tree node advances by its text line, not by its frame, so the spacing
    // is derived from the band height rather than set to the margin.
    const float rowPitch = ImGui::GetFrameHeight() + kRowMargin * 2.0f;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                        ImVec2{ImGui::GetStyle().ItemSpacing.x,
                               rowPitch - ImGui::GetTextLineHeight()});
    ImGui::BeginChild("##scene_tree", ImVec2(0, -kDropZoneH), ImGuiChildFlags_None,
                      ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoBackground);

    // The band reaches above the cursor; flush against the panel top the first
    // row would have it clipped.
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + kRowMargin +
                         std::round(ui::TextOpticalOffsetY()));

    std::vector<entt::entity> roots;
    for (auto e : reg.storage<entt::entity>())
    {
        if (!reg.valid(e))
            continue;
        auto* hc = reg.try_get<Hierarchy_C>(e);
        if (hc && hc->parent != entt::null)
            continue;
        roots.push_back(e);
    }
    sortByName(reg, roots);

    for (entt::entity e : roots)
        drawEntityNode(world, e, selectedEntity);

    ImGui::EndChild();
    ImGui::PopStyleVar(3);

    // Zone de drop fixe toujours visible en bas → détache l'entité draguée
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0, 0, 0, 0});
    ImGui::PushStyleColor(ImGuiCol_Text, ui::textDim);
    if (ImGui::Button(ICON_MD_ADD "  Add entity", {-FLT_MIN, 0.f}))
        ImGui::OpenPopup("AddEntityPopup");
    ImGui::PopStyleColor(2);

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

    if (pendingDuplicate_)
    {
        selectedEntity = duplicateEntity(world, *pendingDuplicate_);
        pendingDuplicate_.reset();
    }
    if (pendingDelete_)
    {
        world.destroy(*pendingDelete_);
        pendingDelete_.reset();
        if (selectedEntity && !reg.valid(selectedEntity->entity_))
            selectedEntity.reset();
    }
}

}  // namespace batap
