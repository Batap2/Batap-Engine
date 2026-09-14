#include "InspectorPanel.h"

#include "UI/FieldUI.h"

#include <algorithm>
#include <filesystem>

#include "App.h"
#include "Assets/AssetManager.h"
#include "Assets/Mesh.h"
#include "Components/Materials_C.h"
#include "Components/Mesh_C.h"
#include "Components/Skybox_C.h"
#include "Components/Transform_C.h"
#include "Instance/InstanceDeclaration.h"
#include "Reflection/ComponentRegistry.h"
#include "Systems/Systems.h"
#include "Systems/Transform_S.h"
#include "UI/AssetHolder.h"
#include "UI/CollapsingGroup.h"
#include "UI/Field.h"
#include "World.h"

#include <imgui.h>
#include <Eigen/Geometry>
#include <cctype>
#include <cfloat>
#include <numbers>

namespace batap
{

static bool removeComponentMenu(const char* id)
{
    bool removed = false;
    if (ImGui::BeginPopupContextItem(id))
    {
        removed = ImGui::MenuItem("Remove Component");
        ImGui::EndPopup();
    }
    return removed;
}

static void removeComponent(World& world, EntityHandle ent, const ComponentType& t)
{
    t.remove(*ent.reg_, ent.entity_);
    world.instances().markDirty(ent, t.mask());
}

// "castShadows" / "pointLight" -> "Cast Shadows" / "Point Light"
static std::string prettyLabel(const std::string& name)
{
    std::string out;
    out.reserve(name.size() + 4);
    for (char c : name)
    {
        if (out.empty())
            out += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        else
        {
            if (std::isupper(static_cast<unsigned char>(c)))
                out += ' ';
            out += c;
        }
    }
    return out;
}

// Registry-declared components: one collapsing group per component, one row
// per field — no per-component editor code.
void InspectorPanel::drawReflected(EntityHandle ent, World& world, App& app)
{
    FieldUIContext ui{&app, &assetPicker_, ent, nullptr};

    for (const ComponentType& t : ComponentRegistry::instance().all())
    {
        if (t.meta.customEditor)  // drawn by its own panel above
            continue;
        if (!t.tryGet)
            continue;

        void* c = t.tryGet(*ent.reg_, ent.entity_);
        if (!c)
            continue;

        // Markers define the entity's aspect: not removable, like not addable.
        const bool removable = (t.mask() & markerComponentMask()) == 0;

        bool changed = false;
        bool removed = false;
        {
            auto g = ui::CollapsingGroup(prettyLabel(t.name).c_str());
            removed = removable && removeComponentMenu(t.name.c_str());
            if (g && !removed)
                if (auto _ = ui::BeginFields(t.name.c_str()))
                    for (const Field& f : t.fields)
                        if (f.type->drawUI)
                        {
                            ui.component_ = &t;
                            changed |= ui::Field(prettyLabel(f.name).c_str(),
                                                 [&] { return f.type->drawUI(f.ptrIn(c), f, ui); });
                        }
        }

        if (removed)
            removeComponent(world, ent, t);
        else if (changed)
        {
            if (t.patch)
                t.patch(*ent.reg_, ent.entity_);
            world.instances().markDirty(ent, t.mask());
        }
    }
}

void InspectorPanel::draw(World& world, App& app, EntityHandle ent)
{
    drawTransform(ent, world);
    drawMesh(ent, app);
    drawMaterials(ent, app);
    drawSkybox(ent, app);
    drawReflected(ent, world, app);
    drawAddComponent(ent, world);
}

void InspectorPanel::drawAddComponent(EntityHandle ent, World& world)
{
    ImGui::Spacing();
    if (ImGui::Button("Add Component", {-FLT_MIN, 0.f}))
        ImGui::OpenPopup("##addComponent");

    if (ImGui::BeginPopup("##addComponent"))
    {
        const ComponentMask markers = markerComponentMask();
        for (const ComponentType& t : ComponentRegistry::instance().all())
        {
            // Markers define the entity's aspect — chosen at spawn, not added.
            if (t.mask() & markers)
                continue;
            if (!t.tryGet || t.tryGet(*ent.reg_, ent.entity_))
                continue;

            if (ImGui::MenuItem(prettyLabel(t.name).c_str()))
            {
                t.getOrEmplace(*ent.reg_, ent.entity_);
                // Same contract as the load path: some components rebuild
                // derived state after their fields exist.
                if (t.meta.onDeserialized)
                    t.meta.onDeserialized(ent, world);
                world.instances().markDirty(ent, t.mask());
            }
        }
        ImGui::EndPopup();
    }
}

// -----------------------------------------------------------------------------

void InspectorPanel::drawTransform(EntityHandle ent, World& world)
{
    if (!ent.try_get<Transform_C>())
        return;

    auto* t = ent.try_get<Transform_C>();

    if (auto g = ui::CollapsingGroup("Transform"))
        if (auto _ = ui::BeginFields("transform"))
        {
            v3f pos = t->pos();
            ui::Field("Position",
                      [&]
                      {
                          ImGui::SetNextItemWidth(-1.0f);
                          if (ImGui::DragFloat3("##pos", pos.data(), 0.05f))
                              ent.setLocalPosition(pos);
                          ui::WrapDragMouse();
                      });

            constexpr float kRadToDeg = 180.0f / std::numbers::pi_v<float>;
            constexpr float kDegToRad = std::numbers::pi_v<float> / 180.0f;

            quatf rot = t->rot().normalized();

            if (!rotationEditEntity_.has_value() || *rotationEditEntity_ != ent)
            {
                rotationEditEntity_ = ent;
                rotationEditSourceQuat_ = rot;
                rotationEditEulerDeg_ =
                    rot.toRotationMatrix().canonicalEulerAngles(0, 1, 2) * kRadToDeg;
            }
            else if (1.0f - std::abs(rotationEditSourceQuat_.dot(rot)) > 1e-4f)
            {
                rotationEditSourceQuat_ = rot;
                rotationEditEulerDeg_ =
                    rot.toRotationMatrix().canonicalEulerAngles(0, 1, 2) * kRadToDeg;
            }

            ui::Field("Rotation",
                      [&]
                      {
                          ImGui::SetNextItemWidth(-1.0f);
                          if (ImGui::DragFloat3("##rot", rotationEditEulerDeg_.data(), 0.1f))
                          {
                              v3f eulerRad = rotationEditEulerDeg_ * kDegToRad;
                              quatf newRot = angleaxisf(eulerRad.x(), v3f::UnitX()) *
                                             angleaxisf(eulerRad.y(), v3f::UnitY()) *
                                             angleaxisf(eulerRad.z(), v3f::UnitZ());
                              newRot.normalize();
                              rotationEditSourceQuat_ = newRot;
                              ent.setLocalRotation(newRot);
                          }
                          ui::WrapDragMouse();
                      });

            v3f scale = t->scale();
            ui::Field("Scale",
                      [&]
                      {
                          ImGui::SetNextItemWidth(-1.0f);
                          if (ImGui::DragFloat3("##scale", scale.data(), 0.01f))
                              ent.setLocalScale(scale);
                          ui::WrapDragMouse();
                      });
        }
}

// -----------------------------------------------------------------------------

void InspectorPanel::drawMesh(EntityHandle ent, App& app)
{
    if (!ent.try_get<Mesh_C>())
        return;

    auto* meshC = ent.try_get<Mesh_C>();

    if (auto g = ui::CollapsingGroup("Mesh"))
    {
        if (auto f = ui::BeginFields("mesh"))
        {
            ui::Field("Mesh",
                      [&]
                      {
                          std::string meshLabel;
                          if (meshC->mesh_)
                              if (auto* p = app.ctx_->assetManager_->getPath(meshC->mesh_))
                                  meshLabel = std::filesystem::path(*p).stem().string();
                          if (AssetHolder({.size_ = v2f(40, 40),
                                           .thumbnail_ = meshC->mesh_ ? 1ull : 0,
                                           .label_ = meshLabel}))
                          {
                              assetPicker_.open(ent, AssetType::Mesh, app.projectDir_);
                          }
                          assetPicker_.draw(app);

                          return true;
                      });
        }
    }
}

// -----------------------------------------------------------------------------

// One slot per submesh, and a mesh with no submesh still draws one.
static uint8_t materialSlotCount(EntityHandle ent, App& app)
{
    auto* meshC = ent.try_get<Mesh_C>();
    if (!meshC || !meshC->mesh_)
        return 1;
    auto* mesh = app.ctx_->assetManager_->get(meshC->mesh_);
    if (!mesh)
        return 1;
    return std::max<uint8_t>(mesh->subMeshCount, 1);
}

void InspectorPanel::drawMaterials(EntityHandle ent, App& app)
{
    auto* mc = ent.try_get<Materials_C>();
    if (!mc)
        return;

    const uint8_t slotCount = materialSlotCount(ent, app);

    bool removed = false;
    {
        auto g = ui::CollapsingGroup("Materials");
        removed = removeComponentMenu("materials");
        if (g && !removed)
        {
            if (auto f = ui::BeginFields("materials"))
            {
                for (uint8_t i = 0; i < slotCount; ++i)
                {
                    const std::string slotLabel = "Slot " + std::to_string(i);
                    ui::Field(
                        slotLabel.c_str(),
                        [&]
                        {
                            std::string matLabel;
                            if (mc->slots[i])
                                if (auto* p = app.ctx_->assetManager_->getPath(mc->slots[i]))
                                    matLabel = std::filesystem::path(*p).stem().string();
                            if (AssetHolder({.size_ = v2f(40, 40),
                                             .thumbnail_ = mc->slots[i] ? 1ull : 0,
                                             .label_ = matLabel.empty() ? "None" : matLabel}))
                                assetPicker_.open(ent, AssetType::Material, app.projectDir_, i);

                            ImGui::SameLine();
                            ImGui::BeginDisabled(!mc->slots[i]);
                            if (ImGui::Button("Edit"))
                                app.uiPanels_.openMaterialEditor(mc->slots[i]);
                            ImGui::EndDisabled();
                            return true;
                        });
                }
            }
            assetPicker_.draw(app);
        }
    }

    if (removed)
        removeComponent(*app.world_, ent, *ComponentRegistry::instance().find("materials"));
}

// -----------------------------------------------------------------------------

void InspectorPanel::drawSkybox(EntityHandle ent, App& app)
{
    auto* sky = ent.try_get<Skybox_C>();
    if (!sky)
        return;

    bool changed = false;

    if (auto g = ui::CollapsingGroup("Skybox"))
        if (auto f = ui::BeginFields("skybox"))
        {
            // Mode selector
            changed |=
                ui::Field("Mode",
                          [&]
                          {
                              static const char* kModes[] = {"HDRI", "Flat Color", "Gradient"};
                              int current = static_cast<int>(sky->mode_);
                              ImGui::SetNextItemWidth(-1.0f);
                              if (ImGui::Combo("##skymode", &current, kModes, 3))
                              {
                                  sky->mode_ = static_cast<Skybox_C::Mode>(current);
                                  return true;
                              }
                              return false;
                          });

            if (sky->mode_ == Skybox_C::Mode::HDRI)
            {
                ui::Field("HDRI",
                          [&]
                          {
                              std::string label;
                              if (sky->hdri_)
                                  if (auto* p = app.ctx_->assetManager_->getPath(sky->hdri_))
                                      label = std::filesystem::path(*p).stem().string();
                              if (AssetHolder({.size_ = v2f(40, 40),
                                               .thumbnail_ = sky->hdri_ ? 1ull : 0,
                                               .label_ = label.empty() ? "None" : label}))
                                  assetPicker_.openHdri(ent, app.projectDir_);
                              assetPicker_.draw(app);
                              return true;
                          });
            }
            else if (sky->mode_ == Skybox_C::Mode::FlatColor)
            {
                changed |=
                    ui::Field("Color",
                              [&]
                              {
                                  ImGui::SetNextItemWidth(-1.0f);
                                  return ImGui::ColorEdit3("##skycolor", sky->color1_.data());
                              });
            }
            else  // Gradient
            {
                changed |= ui::Field("Ciel",
                                     [&]
                                     {
                                         ImGui::SetNextItemWidth(-1.0f);
                                         return ImGui::ColorEdit3("##skyciel", sky->color1_.data());
                                     });
                changed |=
                    ui::Field("Horizon",
                              [&]
                              {
                                  ImGui::SetNextItemWidth(-1.0f);
                                  return ImGui::ColorEdit3("##skyhorizon", sky->color2_.data());
                              });
                changed |= ui::Field("Bas",
                                     [&]
                                     {
                                         ImGui::SetNextItemWidth(-1.0f);
                                         return ImGui::ColorEdit3("##skybas", sky->color3_.data());
                                     });
                changed |= ui::Field("Taille horizon",
                                     [&]
                                     {
                                         ImGui::SetNextItemWidth(-1.0f);
                                         return ImGui::SliderFloat("##skyhw", &sky->horizonWidth_,
                                                                   0.01f, 1.0f);
                                     });
            }

            changed |= ui::FieldDragFloat("Intensity", &sky->intensity_, 0.01f);
        }

    if (changed)
        app.world_->instances().markDirty<Skybox_C>(ent);
}

}  // namespace batap
