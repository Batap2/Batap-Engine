#include "FieldUI.h"

#include "App.h"
#include "Assets/AssetHandle.h"
#include "Assets/AssetManager.h"
#include "Components/RigidBody_C.h"
#include "EigenTypes.h"
#include "UI/AssetPickerPopup.h"
#include "Reflection/ComponentRegistry.h"
#include "UI/Field.h"

#include <imgui.h>

#include <filesystem>
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

const char* kindName(Shape::Kind k)
{
    switch (k)
    {
        case Shape::Kind::Sphere:
            return "Sphere";
        case Shape::Kind::Capsule:
            return "Capsule";
        case Shape::Kind::Box:
            break;
    }
    return "Box";
}

template <class M>
void set(DrawFn fn)
{
    auto& slot = fieldTypeSlot<M>();
    slot.drawUI = fn;
    drawUIByName()[slot.typeName] = fn;
}

template <class A, AssetType Type>
void setAssetHandle()
{
    set<AssetHandle<A>>(
        [](void* p, const Field& f, FieldUIContext& ui)
        {
            auto& handle = *static_cast<AssetHandle<A>*>(p);
            if (!ui.app_ || !ui.picker_ || !ui.component_)
                return false;

            std::string label = "None";
            if (handle)
                if (const std::string* path = ui.app_->ctx_->assetManager_->getPath(handle))
                    label = std::filesystem::path(*path).stem().string();
            label += "##v";

            if (ImGui::Button(label.c_str(), ImVec2(-1.f, 0.f)))
                ui.picker_->openField(ui.ent_, *ui.component_, f, Type, ui.app_->projectDir_);

            // OpenPopup and BeginPopup have to share an ID scope, so the popup
            // is drawn right here and not once per component.
            return ui.picker_->draw(*ui.app_);
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
            ImGui::SetNextItemWidth(-1.0f);
            const bool changed = ImGui::DragFloat("##v", static_cast<float*>(p), f.meta.speed,
                                                  f.meta.min, f.meta.max);
            ui::WrapDragMouse();
            return changed;
        });

    set<bool>([](void* p, const Field&, FieldUIContext&) { return ImGui::Checkbox("##v", static_cast<bool*>(p)); });

    set<int32_t>(
        [](void* p, const Field& f, FieldUIContext&)
        {
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
            ImGui::SetNextItemWidth(-1.0f);
            const bool changed = ImGui::DragScalar("##v", ImGuiDataType_U32, p, f.meta.speed);
            ui::WrapDragMouse();
            return changed;
        });

    set<v3f>(
        [](void* p, const Field& f, FieldUIContext&)
        {
            auto* v = static_cast<v3f*>(p);
            ImGui::SetNextItemWidth(-1.0f);
            const bool changed = ImGui::DragFloat3("##v", v->data(), f.meta.speed);
            ui::WrapDragMouse();
            return changed;
        });

    setAssetHandle<Mesh, AssetType::Mesh>();
    setAssetHandle<Texture, AssetType::Texture>();
    setAssetHandle<Material, AssetType::Material>();

    set<col3>(
        [](void* p, const Field&, FieldUIContext&)
        {
            ImGui::SetNextItemWidth(-1.0f);
            return ImGui::ColorEdit3("##v", static_cast<col3*>(p)->data());
        });

    set<std::vector<Shape>>(
        [](void* p, const Field&, FieldUIContext&)
        {
            auto& shapes = *static_cast<std::vector<Shape>*>(p);
            bool changed = false;

            if (ImGui::SmallButton("Add"))
            {
                shapes.push_back(Shape{});
                changed = true;
            }

            auto removed = shapes.end();
            int id = 0;
            for (auto it = shapes.begin(); it != shapes.end(); ++it, ++id)
            {
                Shape& s = *it;
                ImGui::PushID(id);
                struct Guard
                {
                    ~Guard() { ImGui::PopID(); }
                } guard;

                const bool open = ImGui::TreeNodeEx("##shape", ImGuiTreeNodeFlags_DefaultOpen, "%s",
                                                    kindName(s.kind_));
                if (shapes.size() > 1)
                {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("x"))
                        removed = it;
                }
                if (!open)
                    continue;

                int kind = static_cast<int>(s.kind_);
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::Combo("##kind", &kind, "Box\0Sphere\0Capsule\0"))
                {
                    s.kind_ = static_cast<Shape::Kind>(kind);
                    changed = true;
                }

                switch (s.kind_)
                {
                    case Shape::Kind::Box:
                        changed |= ImGui::DragFloat3("Half extents", s.halfExtents_.data(), 0.01f,
                                                     0.001f, 10000.f);
                        break;
                    case Shape::Kind::Sphere:
                        changed |=
                            ImGui::DragFloat("Radius", &s.radius_, 0.01f, 0.001f, 10000.f);
                        break;
                    case Shape::Kind::Capsule:
                        changed |=
                            ImGui::DragFloat("Radius", &s.radius_, 0.01f, 0.001f, 10000.f);
                        changed |= ImGui::DragFloat("Half height", &s.halfHeight_, 0.01f, 0.001f,
                                                    10000.f);
                        break;
                }

                changed |= ImGui::DragFloat3("Offset", s.localPos_.data(), 0.01f);
                changed |= ImGui::DragFloat3("Rotation", s.localRotDeg_.data(), 0.5f);
                ui::WrapDragMouse();

                ImGui::TreePop();
            }

            if (removed != shapes.end())
            {
                shapes.erase(removed);
                changed = true;
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
