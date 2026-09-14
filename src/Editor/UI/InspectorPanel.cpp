#include "InspectorPanel.h"

#include "UI/FieldUI.h"

#include <algorithm>
#include <array>
#include <filesystem>

#include "App.h"
#include "Assets/AssetManager.h"
#include "Assets/Mesh.h"
#include "Components/Materials_C.h"
#include "Components/Name_C.h"
#include "Components/Mesh_C.h"
#include "Components/Skybox_C.h"
#include "Components/Transform_C.h"
#include "Instance/InstanceDeclaration.h"
#include "Instance/Spawnable.h"
#include "Reflection/ComponentRegistry.h"
#include "Systems/Systems.h"
#include "Systems/Transform_S.h"
#include "UI/IconsMaterialDesign.h"
#include "UI/ComponentSection.h"
#include "UI/Field.h"
#include "World.h"

#include <imgui.h>
#include <Eigen/Geometry>
#include <cctype>
#include <cfloat>
#include <numbers>

namespace batap
{

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
        ui::ComponentSection section(prettyLabel(t.name).c_str(), t.meta.color, removable);
        if (section)
            if (auto _ = ui::BeginFields(t.name.c_str()))
                for (const Field& f : t.fields)
                    if (f.type->drawUI)
                    {
                        ui.component_ = &t;
                        changed |= ui::Field(prettyLabel(f.name).c_str(),
                                             [&] { return f.type->drawUI(f.ptrIn(c), f, ui); });
                    }

        if (section.removeClicked())
            removeComponent(world, ent, t);
        else if (changed)
        {
            if (t.patch)
                t.patch(*ent.reg_, ent.entity_);
            world.instances().markDirty(ent, t.mask());
        }
    }
}

void InspectorPanel::drawHeader(EntityHandle ent)
{
    const Spawnable& kind = spawnableFor(*ent.reg_, ent.entity_);

    ImGui::PushStyleColor(ImGuiCol_Text, ui::colorOf(kind.color));
    ImGui::TextUnformatted(kind.icon);
    ImGui::PopStyleColor();

    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, ui::textBright);
    ImGui::TextUnformatted(ent.reg_->get<Name_C>(ent.entity_).name_.c_str());
    ImGui::PopStyleColor();

    const std::string id = "#" + std::to_string(entt::to_integral(ent.entity_));
    ui::PushSmallFont small;
    const float w = ImGui::CalcTextSize(id.c_str()).x;
    ImGui::SameLine(ImGui::GetContentRegionMax().x - w);
    ImGui::TextDisabled("%s", id.c_str());

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
}

void InspectorPanel::draw(World& world, App& app, EntityHandle ent)
{
    drawHeader(ent);
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
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0, 0, 0, 0});
    ImGui::PushStyleColor(ImGuiCol_Text, ui::textDim);
    const bool add = ImGui::Button(ICON_MD_ADD "  Add component", {-FLT_MIN, 0.f});
    ImGui::PopStyleColor(2);
    if (add)
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

    ui::ComponentSection section("Transform", ComponentColor::Blue);
    if (section)
        if (auto _ = ui::BeginFields("transform"))
        {
            v3f pos = t->pos();
            if (ui::FieldDragFloat3("Position", pos.data(), 0.05f))
                ent.setLocalPosition(pos);

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

            if (ui::FieldDragFloat3("Rotation", rotationEditEulerDeg_.data(), 0.1f))
            {
                v3f eulerRad = rotationEditEulerDeg_ * kDegToRad;
                quatf newRot = angleaxisf(eulerRad.x(), v3f::UnitX()) *
                               angleaxisf(eulerRad.y(), v3f::UnitY()) *
                               angleaxisf(eulerRad.z(), v3f::UnitZ());
                newRot.normalize();
                rotationEditSourceQuat_ = newRot;
                ent.setLocalRotation(newRot);
            }

            v3f scale = t->scale();
            if (ui::FieldDragFloat3("Scale", scale.data(), 0.01f))
                ent.setLocalScale(scale);
        }
}

// -----------------------------------------------------------------------------

void InspectorPanel::drawMesh(EntityHandle ent, App& app)
{
    if (!ent.try_get<Mesh_C>())
        return;

    auto* meshC = ent.try_get<Mesh_C>();

    std::string meshSummary;
    if (meshC->mesh_)
        if (auto* p = app.ctx_->assetManager_->getPath(meshC->mesh_))
            meshSummary = std::filesystem::path(*p).stem().string();

    ui::ComponentSection section("Mesh", ComponentColor::Violet, false, meshSummary.c_str());
    if (section)
    {
        if (auto f = ui::BeginFields("mesh"))
        {
            ui::Field("Asset",
                      [&]
                      {
                          std::string meshLabel;
                          if (meshC->mesh_)
                              if (auto* p = app.ctx_->assetManager_->getPath(meshC->mesh_))
                                  meshLabel = std::filesystem::path(*p).stem().string();
                          if (ui::AssetField(ICON_MD_VIEW_IN_AR,
                                             meshLabel.empty() ? "None" : meshLabel.c_str(),
                                             ui::colorOf(ComponentColor::Violet)))
                              assetPicker_.open(ent, AssetType::Mesh, app.projectDir_);
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
        const std::string slotSummary =
            std::to_string(slotCount) + (slotCount > 1 ? " slots" : " slot");
        ui::ComponentSection section("Materials", ComponentColor::Yellow, true,
                                     slotSummary.c_str());
        removed = section.removeClicked();
        if (section && !removed)
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
                            if (ui::AssetField(ICON_MD_PALETTE,
                                               matLabel.empty() ? "None" : matLabel.c_str(),
                                               ui::colorOf(ComponentColor::Yellow)))
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

    ui::ComponentSection section("Skybox", ComponentColor::Cyan);
    if (section)
        if (auto f = ui::BeginFields("skybox"))
        {
            // Mode selector
            changed |=
                ui::Field("Mode",
                          [&]
                          {
                              static constexpr std::array<const char*, 3> kModes = {
                                  "HDRI", "Flat Color", "Gradient"};
                              int current = static_cast<int>(sky->mode_);
                              if (!ui::ComboField("##skymode", &current, kModes))
                                  return false;
                              sky->mode_ = static_cast<Skybox_C::Mode>(current);
                              return true;
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
                              if (ui::AssetField(ICON_MD_PANORAMA,
                                                 label.empty() ? "None" : label.c_str(),
                                                 ui::colorOf(ComponentColor::Cyan)))
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
                                  return ui::ColorFieldRaw("##skycolor", sky->color1_.data(), 3);
                              });
            }
            else  // Gradient
            {
                changed |= ui::Field("Ciel",
                                     [&]
                                     {
                                         ImGui::SetNextItemWidth(-1.0f);
                                         return ui::ColorFieldRaw("##skyciel", sky->color1_.data(), 3);
                                     });
                changed |=
                    ui::Field("Horizon",
                              [&]
                              {
                                  return ui::ColorFieldRaw("##skyhorizon", sky->color2_.data(), 3);
                              });
                changed |= ui::Field("Bas",
                                     [&]
                                     {
                                         ImGui::SetNextItemWidth(-1.0f);
                                         return ui::ColorFieldRaw("##skybas", sky->color3_.data(), 3);
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
