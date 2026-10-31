#include "FieldUI.h"

#include "App.h"
#include "Assets/AssetHandle.h"
#include "Assets/AssetManager.h"
#include "ColliderHandles.h"
#include "ColliderShapes.h"
#include "Components/EditorOnly_C.h"
#include "Components/EntityRef.h"
#include "Components/Name_C.h"
#include "Components/RigidBody_C.h"
#include "EigenTypes.h"
#include "Instance/Spawnable.h"
#include "Reflection/ComponentRegistry.h"
#include "UI/AssetPickerPopup.h"
#include "UI/AssetRow.h"
#include "UI/Field.h"
#include "UI/IconsMaterialDesign.h"
#include "UI/ScenePanel.h"
#include "UI/Scoped.h"
#include "UI/UITheme.h"

#include <imgui.h>

#include <cfloat>
#include <string>
#include <unordered_map>
#include <vector>

namespace batap
{

namespace
{
using DrawFn = bool (*)(void*, const Field&, FieldUIContext&);

std::unordered_map<std::string, DrawFn>& drawUIByName()
{
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wexit-time-destructors"
    static std::unordered_map<std::string, DrawFn> map;
#pragma clang diagnostic pop
    return map;
}

void* meshOfShape(void* fieldPtr, size_t index)
{
    auto& shapes = *static_cast<std::vector<Shape>*>(fieldPtr);
    return index < shapes.size() ? static_cast<void*>(&shapes[index].mesh_) : nullptr;
}

template <class M>
void set(DrawFn fn)
{
    auto& slot = fieldTypeSlot<M>();
    slot.drawUI = fn;
    drawUIByName()[slot.typeName] = fn;
}

template <class M>
bool drawEnum(void* p, const Field& f)
{
    auto& v = *static_cast<M*>(p);
    std::vector<const char*> names;
    int current = -1;
    for (size_t i = 0; i < f.enumValues.size(); ++i)
    {
        names.push_back(f.enumValues[i].name.c_str());
        if (f.enumValues[i].value == static_cast<int64_t>(v))
            current = static_cast<int>(i);
    }
    if (!ui::ComboField("##v", &current, names))
        return false;
    v = static_cast<M>(f.enumValues[static_cast<size_t>(current)].value);
    return true;
}

bool drawEntityRef(void* p, const Field&, FieldUIContext& fieldCtx)
{
    if (!fieldCtx.ent_.reg_)
        return false;
    entt::registry& reg = *fieldCtx.ent_.reg_;
    auto& ref = *static_cast<EntityRef*>(p);

    const bool valid = reg.valid(ref.entity_) && reg.all_of<Name_C>(ref.entity_);
    if (valid)
    {
        const Spawnable& kind = spawnableFor(reg, ref.entity_);
        if (ui::AssetField(kind.icon, reg.get<Name_C>(ref.entity_).name_.c_str(),
                           ui::colorOfKind(kind.id)))
            ImGui::OpenPopup("##entityRef");
    }
    else if (ui::AssetField(ICON_MD_LINK_OFF, "None", ui::textDim))
        ImGui::OpenPopup("##entityRef");

    bool changed = false;
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kEntityPayload))
        {
            ref.entity_ = *static_cast<const entt::entity*>(payload->Data);
            changed = true;
        }
        ImGui::EndDragDropTarget();
    }

    ImGui::SetNextWindowSizeConstraints({ImGui::GetItemRectSize().x, 0.0f},
                                        {FLT_MAX, ImGui::GetTextLineHeightWithSpacing() * 16.0f});
    if (!ImGui::BeginPopup("##entityRef"))
        return changed;

    if (ImGui::Selectable("None", !valid))
    {
        ref.entity_ = entt::null;
        changed = true;
    }

    std::vector<entt::entity> entities;
    for (auto e : reg.storage<entt::entity>())
        if (reg.valid(e) && reg.all_of<Name_C>(e) && !reg.all_of<EditorOnly_C>(e))
            entities.push_back(e);
    sortByName(reg, entities);

    for (entt::entity e : entities)
    {
        ui::ScopedID id{static_cast<int>(entt::to_integral(e))};
        const std::string label =
            std::string(spawnableFor(reg, e).icon) + "  " + reg.get<Name_C>(e).name_;
        if (ImGui::Selectable(label.c_str(), e == ref.entity_))
        {
            ref.entity_ = e;
            changed = true;
        }
    }

    ImGui::EndPopup();
    return changed;
}

// The picker applies the pick itself (patch + markDirty), so the row never
// reports a change of its own.
template <class A, AssetType Type>
void setAssetHandle()
{
    set<AssetHandle<A>>(
        [](void* p, const Field& f, FieldUIContext& fieldCtx)
        {
            auto& handle = *static_cast<AssetHandle<A>*>(p);
            if (!fieldCtx.app_ || !fieldCtx.picker_ || !fieldCtx.component_)
                return false;

            const std::string name = ui::assetName(*fieldCtx.app_->ctx_->assetManager_, handle);
            if (ui::AssetRow(Type, name))
                fieldCtx.picker_->openField(fieldCtx.ent_, *fieldCtx.component_, f, Type,
                                            fieldCtx.app_->projectDir_);
            return false;
        });
}
}  // namespace

// Installs the editor half of each field type: how it is drawn. Serialization
// halves live in the engine (registerBuiltinFieldTypes); a game build without
// the editor simply never fills drawUI and never calls it.
void installFieldUI()
{
    set<float>(
        [](void* p, const Field& f, FieldUIContext&)
        {
            auto* v = static_cast<float*>(p);
            if (f.meta.min < f.meta.max)
                return ui::DragGauge(v, f.meta.min, f.meta.max, f.meta.speed);
            return ui::DragValue("##v", v, ImGui::GetContentRegionAvail().x, f.meta.speed,
                                 f.meta.min, f.meta.max);
        });

    set<bool>([](void* p, const Field&, FieldUIContext&)
              { return ImGui::Checkbox("##v", static_cast<bool*>(p)); });

    set<int32_t>(
        [](void* p, const Field& f, FieldUIContext&)
        {
            if (!f.enumValues.empty())
                return drawEnum<int32_t>(p, f);
            ImGui::SetNextItemWidth(-1.0f);
            const bool changed = ImGui::DragInt("##v", static_cast<int32_t*>(p), f.meta.speed,
                                                static_cast<int>(f.meta.min),
                                                static_cast<int>(f.meta.max));
            ui::WrapDragMouse();
            return changed;
        });

    set<uint32_t>(
        [](void* p, const Field& f, FieldUIContext&)
        {
            if (!f.enumValues.empty())
                return drawEnum<uint32_t>(p, f);
            ImGui::SetNextItemWidth(-1.0f);
            const bool changed = ImGui::DragScalar("##v", ImGuiDataType_U32, p, f.meta.speed);
            ui::WrapDragMouse();
            return changed;
        });

    set<v3f>(
        [](void* p, const Field& f, FieldUIContext&)
        {
            auto* v = static_cast<v3f*>(p);
            return ui::DragFloatN(ui::spanOf(v->data(), 3), f.meta.speed);
        });

    set<EntityRef>(drawEntityRef);

    setAssetHandle<Mesh, AssetType::Mesh>();
    setAssetHandle<Texture, AssetType::Texture>();
    setAssetHandle<Material, AssetType::Material>();

    set<col3>([](void* p, const Field&, FieldUIContext&)
              { return ui::ColorField("##v", static_cast<col3*>(p)->data(), 3); });

    set<std::vector<Shape>>(
        [](void* p, const Field& f, FieldUIContext& fieldCtx)
        {
            auto& shapes = *static_cast<std::vector<Shape>*>(p);
            App* app = fieldCtx.app_;
            ColliderHandles* handles = app ? &app->uiPanels_.colliderHandles() : nullptr;
            const int selected = handles ? handles->selected(fieldCtx.ent_) : -1;
            bool changed = false;

            int removed = -1;
            int id = 0;
            for (auto it = shapes.begin(); it != shapes.end(); ++it, ++id)
            {
                Shape& s = *it;
                ui::ScopedID scopedId{id};

                ImGuiTreeNodeFlags flags =
                    ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow |
                    ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowOverlap |
                    ImGuiTreeNodeFlags_NoTreePushOnOpen;
                if (id == selected)
                    flags |= ImGuiTreeNodeFlags_Selected;
                const bool open =
                    ImGui::TreeNodeEx("##shape", flags, "%s", colliderShape::kindName(s.kind_));
                const bool rowClicked = ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen();
                const ImVec2 rowMin = ImGui::GetItemRectMin();
                const ImVec2 rowMax = ImGui::GetItemRectMax();

                const float side = ImGui::GetFrameHeight();
                const float gap = ImGui::GetStyle().ItemInnerSpacing.x;
                bool buttonClicked = false;
                ImGui::SameLine();
                ImGui::SetCursorScreenPos({rowMax.x - side * 2.0f - gap, rowMin.y});
                {
                    ui::ScopedColor flat{{ImGuiCol_Button, ui::transparent}};
                    if (ui::IconButton(ICON_MD_FIT_SCREEN "##fit", side))
                    {
                        buttonClicked = true;
                        if (app && colliderShape::fit(*app->ctx_, fieldCtx.ent_, s))
                            changed = true;
                        else if (app)
                            app->showToast("No mesh to fit the shape to");
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Fit to the mesh");

                    ImGui::SameLine(0.0f, gap);
                    ImGui::BeginDisabled(shapes.size() <= 1);
                    ui::ScopedColor danger{{ImGuiCol_ButtonHovered, ui::red}};
                    if (ui::IconButton(ICON_MD_CLOSE "##remove", side))
                    {
                        buttonClicked = true;
                        removed = id;
                    }
                    ImGui::EndDisabled();
                }
                if (rowClicked && !buttonClicked && handles)
                    handles->select(fieldCtx.ent_, id == selected ? -1 : id);
                if (!open)
                    continue;

                auto fields = ui::BeginFields("shapeFields");
                if (!fields)
                    continue;

                changed |= ui::Field("Kind",
                                     [&]
                                     {
                                         static constexpr std::array<const char*, 4> kKinds = {
                                             "Box", "Sphere", "Capsule", "Mesh"};
                                         int kind = static_cast<int>(s.kind_);
                                         if (!ui::ComboField("##kind", &kind, kKinds))
                                             return false;
                                         s.kind_ = static_cast<Shape::Kind>(kind);
                                         return true;
                                     });

                switch (s.kind_)
                {
                    case Shape::Kind::Box:
                        changed |= ui::Field("Half size",
                                             [&] {
                                                 return ui::DragFloatN(
                                                     ui::spanOf(s.halfExtents_.data(), 3), 0.01f,
                                                     0.001f, 10000.f);
                                             });
                        break;
                    case Shape::Kind::Sphere:
                        changed |= ui::FieldDragFloat("Radius", &s.radius_, 0.01f, 0.001f, 10000.f);
                        break;
                    case Shape::Kind::Capsule:
                        changed |= ui::FieldDragFloat("Radius", &s.radius_, 0.01f, 0.001f, 10000.f);
                        changed |= ui::FieldDragFloat("Half height", &s.halfHeight_, 0.01f, 0.001f,
                                                      10000.f);
                        break;
                    case Shape::Kind::Mesh:
                        // The picker applies the pick itself (patch +
                        // markDirty), so the row reports no change of its own.
                        ui::Field("Mesh",
                                  [&]
                                  {
                                      AssetManager* assets = app ? app->assetManager_ : nullptr;
                                      const std::string name =
                                          assets ? ui::assetName(*assets, s.mesh_) : std::string{};
                                      if (ui::AssetRow(AssetType::Mesh, name) && assets &&
                                          fieldCtx.picker_ && fieldCtx.component_)
                                          fieldCtx.picker_->openField(
                                              fieldCtx.ent_, *fieldCtx.component_, f,
                                              AssetType::Mesh, app->projectDir_, meshOfShape,
                                              static_cast<size_t>(id));
                                  });
                        break;
                }

                changed |= ui::FieldDragFloat3("Offset", s.localPos_.data(), 0.01f);
                changed |= ui::FieldDragFloat3("Rotation", s.localRotDeg_.data(), 0.5f);
            }

            if (removed >= 0)
            {
                shapes.erase(shapes.begin() + removed);
                if (handles)
                    handles->select(fieldCtx.ent_, -1);
                changed = true;
            }

            if (ui::AddButton(ICON_MD_ADD "  Add shape"))
                ImGui::OpenPopup("##addShape");
            if (ImGui::BeginPopup("##addShape"))
            {
                for (const Shape::Kind kind : {Shape::Kind::Box, Shape::Kind::Sphere,
                                               Shape::Kind::Capsule, Shape::Kind::Mesh})
                    if (ImGui::MenuItem(colliderShape::kindName(kind)))
                    {
                        Shape added;
                        added.kind_ = kind;
                        if (app)
                            colliderShape::fit(*app->ctx_, fieldCtx.ent_, added);
                        shapes.push_back(added);
                        if (handles)
                            handles->select(fieldCtx.ent_, static_cast<int>(shapes.size()) - 1);
                        changed = true;
                    }
                ImGui::EndPopup();
            }
            return changed;
        });
}

bool installFieldUIFor(FieldType& type)
{
    auto it = drawUIByName().find(type.typeName);
    if (it == drawUIByName().end())
        return false;
    type.drawUI = it->second;
    return true;
}

}  // namespace batap
