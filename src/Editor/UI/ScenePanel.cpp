#include "ScenePanel.h"

#include "App.h"
#include "Components/EditorOnly_C.h"
#include "Components/FileId_C.h"
#include "Components/Hierarchy_C.h"
#include "Components/Name_C.h"
#include "Components/TemplateInstance_C.h"
#include "Components/TemplateMember_C.h"
#include "Instance/InstanceManager.h"
#include "Instance/Spawnable.h"
#include "Reflection/ComponentRegistry.h"
#include "Renderer/Renderer.h"
#include "Serialization/TemplateInstances.h"
#include "Systems/Hierarchy_S.h"
#include "UI/ContentBrowser.h"
#include "UI/Field.h"
#include "UI/IconsMaterialDesign.h"
#include "UI/Scoped.h"
#include "UI/UITheme.h"
#include "World.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "misc/cpp/imgui_stdlib.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <vector>

namespace batap
{
void sortByName(entt::registry& reg, std::vector<entt::entity>& entities)
{
    std::sort(entities.begin(), entities.end(),
              [&](entt::entity a, entt::entity b)
              {
                  const std::string& na = reg.get<Name_C>(a).name_;
                  const std::string& nb = reg.get<Name_C>(b).name_;
                  return std::lexicographical_compare(
                      na.begin(), na.end(), nb.begin(), nb.end(),
                      [](char x, char y)
                      {
                          return std::tolower(static_cast<unsigned char>(x)) <
                                 std::tolower(static_cast<unsigned char>(y));
                      });
              });
}

namespace
{
constexpr const char* kAddEntityPopup = "##addEntity";

std::vector<entt::entity> sortedChildren(EntityHandle parent)
{
    std::vector<entt::entity> children;
    for (entt::entity child : Hierarchy_S::children(parent))
        children.push_back(child);
    sortByName(*parent.reg_, children);
    return children;
}

EntityHandle duplicateTree(World& world, App& app, EntityHandle src, bool keepTags)
{
    auto& reg = *src.reg_;
    EntityHandle dst = world.spawn(Spawnables[0]);
    reg.get<Name_C>(dst.entity_).name_ = reg.get<Name_C>(src.entity_).name_;
    if (keepTags)
    {
        if (reg.all_of<TemplateMember_C>(src.entity_))
            reg.emplace<TemplateMember_C>(dst.entity_);
        if (auto* fid = reg.try_get<FileId_C>(src.entity_))
            reg.emplace<FileId_C>(dst.entity_, *fid);
        if (auto* ti = reg.try_get<TemplateInstance_C>(src.entity_))
            reg.emplace<TemplateInstance_C>(dst.entity_, *ti);
    }

    for (const ComponentType& t : ComponentRegistry::instance().all())
    {
        if (!t.tryGet || !t.tryGet(reg, src.entity_))
            continue;
        t.copy(reg, src.entity_, dst.entity_);
        if (!t.tryGet(reg, dst.entity_))
        {
            app.showToast(t.refusal(reg, dst.entity_));
            continue;
        }
        if (t.meta.onDeserialized)
            t.meta.onDeserialized(dst, world);
        world.instances().markDirty(dst, t.mask());
    }

    for (entt::entity child : sortedChildren(src))
        Hierarchy_S::attach(dst, duplicateTree(world, app, {&reg, child}, keepTags));

    if (auto* hc = reg.try_get<Hierarchy_C>(src.entity_); hc && hc->parent != entt::null)
        Hierarchy_S::attach({&reg, hc->parent}, dst);

    return dst;
}
}  // namespace

EntityHandle duplicateEntity(World& world, App& app, EntityHandle src)
{
    auto& reg = *src.reg_;
    const bool instance = reg.all_of<TemplateInstance_C>(src.entity_);
    EntityHandle dst = duplicateTree(world, app, src, instance);
    if (instance)
    {
        reg.remove<TemplateMember_C>(dst.entity_);
        reg.remove<FileId_C>(dst.entity_);
    }
    return dst;
}

namespace
{
void dropEntity(entt::registry& reg, const ImGuiPayload& payload,
                std::optional<EntityHandle> newParent)
{
    EntityHandle dragged{&reg, *static_cast<const entt::entity*>(payload.Data)};
    if (!newParent)
        dragged.setParent(EntityHandle{});
    else if (newParent->entity_ != dragged.entity_)
        dragged.setParent(*newParent);
}

void acceptEntityDrop(entt::registry& reg, EntityHandle newParent,
                      std::optional<ScenePanel::ContentDrop>& drop)
{
    if (!ImGui::BeginDragDropTarget())
        return;
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kEntityPayload))
        dropEntity(reg, *payload, newParent);
    else if (const ImGuiPayload* file = ImGui::AcceptDragDropPayload(kContentPayload))
        drop = ScenePanel::ContentDrop{static_cast<const char*>(file->Data), newParent};
    ImGui::EndDragDropTarget();
}

void acceptRootDrop(entt::registry& reg, std::optional<ScenePanel::ContentDrop>& drop)
{
    ImGuiWindow* tree = ImGui::GetCurrentWindow();
    const ImRect inner = tree->InnerRect;
    if (!ImGui::BeginDragDropTargetCustom(inner, tree->GetID("##sceneRoot")))
        return;
    constexpr ImGuiDragDropFlags flags = ImGuiDragDropFlags_AcceptBeforeDelivery |
                                         ImGuiDragDropFlags_AcceptNoDrawDefaultRect;
    const ImGuiPayload* entity = ImGui::AcceptDragDropPayload(kEntityPayload, flags);
    const ImGuiPayload* file = entity ? nullptr
                                      : ImGui::AcceptDragDropPayload(kContentPayload, flags);
    if (const ImGuiPayload* payload = entity ? entity : file)
    {
        if (payload->Preview)
            tree->DrawList->AddRect({inner.Min.x + 1.0f, inner.Min.y + 1.0f},
                                    {inner.Max.x - 1.0f, inner.Max.y - 1.0f},
                                    ImGui::GetColorU32(ImGuiCol_DragDropTarget), 0.0f, 0, 2.0f);
        if (payload->Delivery && entity)
            dropEntity(reg, *payload, std::nullopt);
        else if (payload->Delivery)
            drop = ScenePanel::ContentDrop{static_cast<const char*>(payload->Data), {}};
    }
    ImGui::EndDragDropTarget();
}
}  // namespace

void ScenePanel::beginRename(EntityHandle ent)
{
    const auto* name = ent.try_get<Name_C>();
    if (!name)
        return;
    renaming_ = ent;
    renameBuffer_ = name->name_;
    renameFocusPending_ = true;
}

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

void ScenePanel::drawEntityNode(World& world, entt::entity e, Selection& selection,
                                const Shortcuts& shortcuts)
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
    const bool onSelectedPath = selected ||
                                (primary.valid() && Hierarchy_S::isDescendantOf(primary, h));

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
    const bool rowDoubleClicked = hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    acceptEntityDrop(reg, h, pendingDrop_);
    if (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
        ImGui::OpenPopup("##rowMenu");
    if (ImGui::BeginPopup("##rowMenu"))
    {
        if (!selection.contains(h))
            selection.set(h);
        if (ImGui::MenuItem("Rename", shortcuts.label(Shortcut::Rename).c_str()))
            beginRename(h);
        if (ImGui::MenuItem("Duplicate", shortcuts.label(Shortcut::Duplicate).c_str()))
            pendingDuplicate_ = h;
        const bool member = owningInstance(reg, e) != entt::null;
        ImGui::BeginDisabled(member);
        if (ImGui::MenuItem("Delete", shortcuts.label(Shortcut::Delete).c_str()))
            pendingDelete_ = h;
        ImGui::EndDisabled();
        if (member && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Part of a template instance: unlink the instance first");
        if (auto* ti = reg.try_get<TemplateInstance_C>(e))
        {
            ImGui::Separator();
            if (ImGui::MenuItem("Open template"))
                pendingOpenTemplate_ = ti->path_;
            if (instanceRootOf(reg, e) == e)
            {
                if (ImGui::MenuItem("Revert to template"))
                    pendingRevert_ = h;
                if (ImGui::MenuItem("Unlink"))
                    pendingUnlink_ = h;
            }
        }
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
    if (renameFocusPending_ && renaming_ && Hierarchy_S::isDescendantOf(*renaming_, h))
        ImGui::SetNextItemOpen(true);

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
    if (reg.all_of<TemplateInstance_C>(e))
        dl->AddText({nodeStart.x + ImGui::GetTreeNodeToLabelSpacing() +
                         ImGui::CalcTextSize(label.c_str()).x + kIconGap,
                     nodeStart.y + ImGui::GetStyle().FramePadding.y - Renderer::kIconGlyphOffsetY},
                    ImGui::GetColorU32(ui::accent), ICON_MD_LINK);

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
    if (rowDoubleClicked && !ImGui::IsItemToggledOpen())
        pendingFocus_ = h;

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
        drawEntityNode(world, child, selection, shortcuts);

    ImGui::TreePop();
}

void ScenePanel::draw(World& world, App& app, Selection& selection, float bottomReserve)
{
    auto& reg = world.registry_;
    rowOrder_.clear();
    if (renaming_ && !reg.valid(renaming_->entity_))
        renaming_.reset();

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
        drawEntityNode(world, e, selection, app.uiPanels_.shortcuts());
    acceptRootDrop(reg, pendingDrop_);

    ImGui::EndChild();
    ImGui::PopStyleVar(3);

    if (ui::AddButton(ICON_MD_ADD "  Add entity"))
        ImGui::OpenPopup(kAddEntityPopup);

    if (ImGui::BeginPopup(kAddEntityPopup))
    {
        for (const Spawnable& s : Spawnables)
        {
            const std::string label = std::string(s.icon) + " " + s.label;
            if (ImGui::MenuItem(label.c_str()) && !world.spawn(s).valid() && s.refusal)
                app.showToast(s.refusal(reg, entt::null));
        }
        ImGui::EndPopup();
    }

    if (pendingRange_)
    {
        const auto from =
            std::find(rowOrder_.begin(), rowOrder_.end(), selection.primary().entity_);
        const auto to = std::find(rowOrder_.begin(), rowOrder_.end(), pendingRange_->entity_);
        if (from != rowOrder_.end() && to != rowOrder_.end())
            for (auto it = std::min(from, to); it <= std::max(from, to); ++it)
                selection.add(EntityHandle{&reg, *it});
        pendingRange_.reset();
    }
    if (pendingFocus_)
    {
        app.focusEditorCamera(*pendingFocus_);
        pendingFocus_.reset();
    }
    if (pendingDuplicate_)
    {
        selection.set(duplicateEntity(world, app, *pendingDuplicate_));
        pendingDuplicate_.reset();
    }
    if (pendingDrop_)
    {
        const auto placed = placeContent(world, app, *app.ctx_, pendingDrop_->path_, std::nullopt,
                                         pendingDrop_->parent_);
        if (!placed.empty())
        {
            selection.clear();
            for (const EntityHandle& h : placed)
                selection.add(h);
        }
        pendingDrop_.reset();
    }
    if (pendingDelete_)
    {
        if (owningInstance(reg, pendingDelete_->entity_) != entt::null)
            app.showToast("Part of a template instance: unlink the instance first");
        else
            world.destroy(*pendingDelete_);
        pendingDelete_.reset();
        selection.prune();
    }
    if (pendingUnlink_)
    {
        unlinkInstance(world, pendingUnlink_->entity_);
        pendingUnlink_.reset();
    }
    if (pendingRevert_)
    {
        const InstanceView view = viewInstance(world, *app.ctx_, pendingRevert_->entity_);
        if (view.flat_.linkable_)
            revertInstance(world, *app.ctx_, view, true);
        else
            app.showToast(view.flat_.why_);
        pendingRevert_.reset();
    }
    if (!pendingOpenTemplate_.empty())
    {
        const std::string path = templateAbsPath(*app.ctx_, pendingOpenTemplate_);
        pendingOpenTemplate_.clear();
        app.uiPanels_.requestTemplate(app, std::filesystem::path(path).make_preferred().string());
    }
}

}  // namespace batap
