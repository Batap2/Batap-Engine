#include "ScenePanel.h"

#include "Components/EditorOnly_C.h"
#include "Components/Hierarchy_C.h"
#include "Components/Name_C.h"
#include "Instance/InstanceManager.h"
#include "Instance/Spawnable.h"
#include "Reflection/ComponentRegistry.h"
#include "Systems/Hierarchy_S.h"
#include "UI/IconsMaterialDesign.h"
#include "Renderer/Renderer.h"
#include "UI/Field.h"
#include "UI/Scoped.h"
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
namespace
{
constexpr const char* kEntityPayload = "ENTITY";
constexpr const char* kAddEntityPopup = "##addEntity";

void sortByName(entt::registry& reg, std::vector<entt::entity>& entities)
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

std::vector<entt::entity> sortedChildren(EntityHandle parent)
{
    std::vector<entt::entity> children;
    for (entt::entity child : Hierarchy_S::children(parent))
        children.push_back(child);
    sortByName(*parent.reg_, children);
    return children;
}

EntityHandle duplicateEntity(World& world, EntityHandle src)
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

    for (entt::entity child : sortedChildren(src))
        Hierarchy_S::attach(dst, duplicateEntity(world, {&reg, child}));

    if (auto* hc = reg.try_get<Hierarchy_C>(src.entity_); hc && hc->parent != entt::null)
        Hierarchy_S::attach({&reg, hc->parent}, dst);

    return dst;
}

void dropEntity(entt::registry& reg, const ImGuiPayload& payload,
                std::optional<EntityHandle> newParent)
{
    const EntityHandle dragged{&reg, *static_cast<const entt::entity*>(payload.Data)};
    if (!newParent)
        Hierarchy_S::detach(dragged);
    else if (newParent->entity_ != dragged.entity_)
        Hierarchy_S::attach(*newParent, dragged);
}

void acceptEntityDrop(entt::registry& reg, EntityHandle newParent)
{
    if (!ImGui::BeginDragDropTarget())
        return;
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kEntityPayload))
        dropEntity(reg, *payload, newParent);
    ImGui::EndDragDropTarget();
}

void acceptRootDrop(entt::registry& reg)
{
    ImGuiWindow* tree = ImGui::GetCurrentWindow();
    const ImRect inner = tree->InnerRect;
    if (!ImGui::BeginDragDropTargetCustom(inner, tree->GetID("##sceneRoot")))
        return;
    constexpr ImGuiDragDropFlags flags =
        ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect;
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kEntityPayload, flags))
    {
        if (payload->Preview)
            tree->DrawList->AddRect({inner.Min.x + 1.0f, inner.Min.y + 1.0f},
                                    {inner.Max.x - 1.0f, inner.Max.y - 1.0f},
                                    ImGui::GetColorU32(ImGuiCol_DragDropTarget), 0.0f, 0, 2.0f);
        if (payload->Delivery)
            dropEntity(reg, *payload, std::nullopt);
    }
    ImGui::EndDragDropTarget();
}
}  // namespace

bool ScenePanel::drawRename(entt::registry& reg, EntityHandle ent)
{
    if (!renaming_ || *renaming_ != ent)
        return false;

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
            reg.get<Name_C>(ent.entity_).name_ = renameBuffer_;
        renaming_.reset();
    }
    return true;
}

void ScenePanel::drawEntityNode(World& world, entt::entity e, Selection& selection)
{
    auto& reg = world.registry_;
    const EntityHandle h{&reg, e};
    rowOrder_.push_back(e);

    if (drawRename(reg, h))
        return;

    const Spawnable& kind = spawnableFor(reg, e);
    const ImU32 kindColor = ImGui::GetColorU32(ui::colorOfKind(kind.id));

    auto* hc = reg.try_get<Hierarchy_C>(e);
    const bool hasChildren = hc && hc->firstChild != entt::null;
    const EntityHandle primary = selection.primary();
    const bool selected = selection.contains(h);
    const bool onSelectedPath =
        selected || (primary.valid() && Hierarchy_S::isDescendantOf(primary, h));

    // The spaces hold the glyph's place: it is painted separately, in the
    // kind's colour, and left in the label it would be drawn twice.
    constexpr float kIconGap = 4.0f;
    const float spaceW = ImGui::CalcTextSize(" ").x;
    const float iconW = ImGui::CalcTextSize(kind.icon).x + kIconGap;
    const std::string pad(static_cast<size_t>(std::ceil(iconW / spaceW)), ' ');
    const std::string label = pad + reg.get<Name_C>(e).name_;

    void* nodeId = reinterpret_cast<void*>(static_cast<uintptr_t>(entt::to_integral(e)));
    const ImVec2 nodeStart = ImGui::GetCursorScreenPos();

    // Before the node, since the fill goes under the text: ImGui's own covers
    // the node rect only and cannot be corrected after. Whole pixels, or
    // antialiased edges make two rows bleed into each other.
    const float optical = std::round(ui::TextOpticalOffsetY());
    const float rowTop = std::round(nodeStart.y) - kRowMargin - optical;
    const float rowBottom = rowTop + ImGui::GetFrameHeight() + kRowMargin * 2.0f;
    const ImRect inner = ImGui::GetCurrentWindow()->InnerRect;

    // The node's own rect stops at the label, so the whole band is claimed here
    // and answers for the row. AllowOverlap leaves the arrow and the drag to the
    // node on top, at the price of the band reporting itself unhovered wherever
    // the node covers it — hence the flag, and the mouse read by hand instead of
    // the button's own press.
    // The band spans the visible width in screen space, outside the layout:
    // left in it, it would stretch the content as the tree is scrolled.
    ImGuiWindow* host = ImGui::GetCurrentWindow();
    const ImVec2 contentMax = host->DC.CursorMaxPos;
    const ImVec2 idealMax = host->DC.IdealMaxPos;

    ImGui::SetCursorScreenPos({inner.Min.x, rowTop});
    ImGui::PushID(nodeId);
    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton("##row", {ImMax(1.0f, inner.Max.x - inner.Min.x), rowBottom - rowTop});
    const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenOverlappedByItem);
    const bool rowClicked = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    acceptEntityDrop(reg, h);
    if (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
        ImGui::OpenPopup("##rowMenu");
    if (ImGui::BeginPopup("##rowMenu"))
    {
        if (!selection.contains(h))
            selection.set(h);
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
    ImGui::PopID();
    ImGui::SetCursorScreenPos(nodeStart);
    host->DC.CursorMaxPos = contentMax;
    host->DC.IdealMaxPos = idealMax;

    if (selected || hovered)
        ImGui::GetWindowDrawList()->AddRectFilled(
            {inner.Min.x, rowTop}, {inner.Max.x, rowBottom},
            ImGui::GetColorU32(selected ? ImGuiCol_Header : ImGuiCol_HeaderHovered));

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (!hasChildren)
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

    bool opened = false;
    {
        // The band above already painted the row, so ImGui's own highlight is
        // pushed out of the way.
        ui::ScopedColor nodeColors{{ImGuiCol_Text, onSelectedPath ? ui::textBright : ui::text},
                                   {ImGuiCol_HeaderHovered, ui::transparent},
                                   {ImGuiCol_HeaderActive, ui::transparent}};
        opened = ImGui::TreeNodeEx(nodeId, flags, "%s", label.c_str()) && hasChildren;
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (selected)
        dl->AddRectFilled({ImGui::GetItemRectMin().x, rowTop},
                          {ImGui::GetItemRectMin().x + 2.0f, rowBottom}, kindColor);

    // The glyph carries the offset keeping icons on the baseline inside text;
    // drawn on its own it has to come back out.
    dl->AddText({nodeStart.x + ImGui::GetTreeNodeToLabelSpacing(),
                 nodeStart.y + ImGui::GetStyle().FramePadding.y - Renderer::kIconGlyphOffsetY},
                kindColor, kind.icon);

    if (rowClicked && !ImGui::IsItemToggledOpen())
    {
        const ImGuiIO& io = ImGui::GetIO();
        if (io.KeyCtrl)
            selection.toggle(h);
        else if (io.KeyShift && primary.valid())
            pendingRange_ = h;
        else
            selection.set(h);
    }

    if (ImGui::BeginDragDropSource())
    {
        ImGui::SetDragDropPayload(kEntityPayload, &e, sizeof(entt::entity));
        ImGui::TextUnformatted(reg.get<Name_C>(e).name_.c_str());
        ImGui::EndDragDropSource();
    }

    if (!opened)
        return;

    // The list is copied first: a drop reparents while it is being walked.
    for (entt::entity child : sortedChildren(h))
        drawEntityNode(world, child, selection);

    ImGui::TreePop();
}

void ScenePanel::draw(World& world, Selection& selection, float bottomReserve)
{
    auto& reg = world.registry_;
    rowOrder_.clear();

    const ImGuiStyle& style = ImGui::GetStyle();
    const float reserve = ImGui::GetFrameHeight() + style.ItemSpacing.y + bottomReserve;

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{1.0f, style.FramePadding.y});
    ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 14.0f);
    // A tree node advances by its text line, not by its frame, so the spacing
    // is derived from the band height rather than set to the margin.
    const float rowPitch = ImGui::GetFrameHeight() + kRowMargin * 2.0f;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                        ImVec2{style.ItemSpacing.x, rowPitch - ImGui::GetTextLineHeight()});
    // The band reaches above the cursor; flush against the panel top the first
    // row would have it clipped. Padding rather than a cursor move: an empty
    // tree would leave the move with no item after it, which ImGui rejects.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        ImVec2{0.0f, kRowMargin + std::round(ui::TextOpticalOffsetY())});
    ImGui::BeginChild("##sceneTree", {0, -reserve}, ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoBackground);
    ImGui::PopStyleVar();

    std::vector<entt::entity> roots;
    for (auto e : reg.storage<entt::entity>())
    {
        if (!reg.valid(e) || reg.all_of<EditorOnly_C>(e))
            continue;
        auto* hc = reg.try_get<Hierarchy_C>(e);
        if (hc && hc->parent != entt::null)
            continue;
        roots.push_back(e);
    }
    sortByName(reg, roots);

    for (entt::entity e : roots)
        drawEntityNode(world, e, selection);
    acceptRootDrop(reg);

    ImGui::EndChild();
    ImGui::PopStyleVar(3);

    if (ui::AddButton(ICON_MD_ADD "  Add entity"))
        ImGui::OpenPopup(kAddEntityPopup);

    if (ImGui::BeginPopup(kAddEntityPopup))
    {
        for (const Spawnable& s : Spawnables)
        {
            const std::string label = std::string(s.icon) + " " + s.label;
            if (ImGui::MenuItem(label.c_str()))
                world.spawn(s);
        }
        ImGui::EndPopup();
    }

    if (pendingRange_)
    {
        const auto from = std::find(rowOrder_.begin(), rowOrder_.end(),
                                    selection.primary().entity_);
        const auto to = std::find(rowOrder_.begin(), rowOrder_.end(), pendingRange_->entity_);
        if (from != rowOrder_.end() && to != rowOrder_.end())
            for (auto it = std::min(from, to); it <= std::max(from, to); ++it)
                selection.add(EntityHandle{&reg, *it});
        pendingRange_.reset();
    }
    if (pendingDuplicate_)
    {
        selection.set(duplicateEntity(world, *pendingDuplicate_));
        pendingDuplicate_.reset();
    }
    if (pendingDelete_)
    {
        world.destroy(*pendingDelete_);
        pendingDelete_.reset();
        selection.prune();
        if (renaming_ && !reg.valid(renaming_->entity_))
            renaming_.reset();
    }
}

}  // namespace batap
