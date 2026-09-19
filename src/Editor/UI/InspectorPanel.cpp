#include "InspectorPanel.h"

#include "UI/FieldUI.h"

#include <algorithm>
#include <array>

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
#include "UI/AssetRow.h"
#include "UI/ComponentSection.h"
#include "UI/Field.h"
#include "UI/Scoped.h"
#include "World.h"

#include <imgui.h>
#include <Eigen/Geometry>
#include <cctype>
#include <cmath>
#include <numbers>

namespace batap
{
namespace
{
constexpr float kRadToDeg = 180.0f / std::numbers::pi_v<float>;
constexpr float kDegToRad = std::numbers::pi_v<float> / 180.0f;

void removeComponent(World& world, EntityHandle ent, const ComponentType& t)
{
    t.remove(*ent.reg_, ent.entity_);
    world.instances().markDirty(ent, t.mask());
}

void removeComponent(World& world, EntityHandle ent, const char* registryName)
{
    if (const ComponentType* t = ComponentRegistry::instance().find(registryName))
        removeComponent(world, ent, *t);
}

// "castShadows" / "pointLight" -> "Cast Shadows" / "Point Light"
std::string prettyLabel(const std::string& name)
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

// One slot per submesh, and a mesh with no submesh still draws one.
uint8_t materialSlotCount(EntityHandle ent, App& app)
{
    auto* meshC = ent.try_get<Mesh_C>();
    if (!meshC || !meshC->mesh_)
        return 1;
    auto* mesh = app.ctx_->assetManager_->get(meshC->mesh_);
    if (!mesh)
        return 1;
    return std::max<uint8_t>(mesh->subMeshCount, 1);
}
}  // namespace

void InspectorPanel::draw(World& world, App& app, EntityHandle ent)
{
    drawHeader(ent);
    drawTransform(ent);
    drawMesh(ent, app);
    drawMaterials(ent, app);
    drawSkybox(ent, app);
    drawReflected(ent, world, app);
    drawAddComponent(ent, world);

    // One picker serves every section above; OpenPopup and BeginPopup both run
    // inside this call, so they see the same ID stack whatever opened it.
    assetPicker_.draw(app);
}

void InspectorPanel::drawHeader(EntityHandle ent)
{
    const Spawnable& kind = spawnableFor(*ent.reg_, ent.entity_);

    ImGui::PushStyleColor(ImGuiCol_Text, ui::colorOfKind(kind.id));
    ImGui::TextUnformatted(kind.icon);
    ImGui::PopStyleColor();

    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, ui::textBright);
    ImGui::TextUnformatted(ent.reg_->get<Name_C>(ent.entity_).name_.c_str());
    ImGui::PopStyleColor();

    const std::string id = "#" + std::to_string(entt::to_integral(ent.entity_));
    ui::ScopedFont small{ui::smallFont};
    ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(id.c_str()).x);
    ImGui::TextDisabled("%s", id.c_str());

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
}

void InspectorPanel::drawTransform(EntityHandle ent)
{
    auto* t = ent.try_get<Transform_C>();
    if (!t)
        return;

    ui::ComponentSection section("Transform", ComponentColor::Blue);
    if (!section)
        return;

    auto fields = ui::BeginFields("transform");
    if (!fields)
        return;

    v3f pos = t->pos();
    if (ui::FieldDragFloat3("Position", pos.data(), 0.05f))
        ent.setLocalPosition(pos);

    // Euler angles are cached: a quaternion has several equivalent triplets,
    // and recomputing one every frame would make the field jump while dragged.
    const quatf rot = t->rot().normalized();
    if (!rotationEditEntity_ || *rotationEditEntity_ != ent ||
        1.0f - std::abs(rotationEditSourceQuat_.dot(rot)) > 1e-4f)
    {
        rotationEditEntity_ = ent;
        rotationEditSourceQuat_ = rot;
        rotationEditEulerDeg_ = rot.toRotationMatrix().canonicalEulerAngles(0, 1, 2) * kRadToDeg;
    }

    if (ui::FieldDragFloat3("Rotation", rotationEditEulerDeg_.data(), 0.1f))
    {
        const v3f eulerRad = rotationEditEulerDeg_ * kDegToRad;
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

void InspectorPanel::drawMesh(EntityHandle ent, App& app)
{
    auto* meshC = ent.try_get<Mesh_C>();
    if (!meshC)
        return;

    const std::string name = ui::assetName(*app.ctx_->assetManager_, meshC->mesh_);

    ui::ComponentSection section("Mesh", ComponentColor::Violet, false, name.c_str());
    if (!section)
        return;

    if (auto fields = ui::BeginFields("mesh"))
        ui::Field("Asset",
                  [&]
                  {
                      if (ui::AssetRow(AssetType::Mesh, name))
                          assetPicker_.open(ent, AssetType::Mesh, app.projectDir_);
                  });
}

void InspectorPanel::drawMaterials(EntityHandle ent, App& app)
{
    auto* mc = ent.try_get<Materials_C>();
    if (!mc)
        return;

    const uint8_t slotCount = materialSlotCount(ent, app);
    const std::string summary =
        std::to_string(slotCount) + (slotCount > 1 ? " slots" : " slot");

    ui::ComponentSection section("Materials", ComponentColor::Yellow, true, summary.c_str());
    if (section)
    {
        if (auto fields = ui::BeginFields("materials"))
            for (uint8_t i = 0; i < slotCount; ++i)
            {
                const std::string label = "Slot " + std::to_string(i);
                ui::Field(label.c_str(),
                          [&]
                          {
                              const std::string name =
                                  ui::assetName(*app.ctx_->assetManager_, mc->slots[i]);
                              const ImGuiStyle& style = ImGui::GetStyle();
                              const float editW =
                                  ImGui::CalcTextSize("Edit").x + style.FramePadding.x * 2.f;
                              const float rowW = ImGui::GetContentRegionAvail().x - editW -
                                                 style.ItemSpacing.x;
                              if (ui::AssetRow(AssetType::Material, name, rowW))
                                  assetPicker_.open(ent, AssetType::Material, app.projectDir_, i);

                              ImGui::SameLine();
                              ImGui::BeginDisabled(!mc->slots[i]);
                              if (ImGui::Button("Edit"))
                                  app.uiPanels_.openMaterialEditor(mc->slots[i]);
                              ImGui::EndDisabled();
                          });
            }
    }

    if (section.removeClicked())
        removeComponent(*app.world_, ent, "materials");
}

void InspectorPanel::drawSkybox(EntityHandle ent, App& app)
{
    auto* sky = ent.try_get<Skybox_C>();
    if (!sky)
        return;

    bool changed = false;

    ui::ComponentSection section("Skybox", ComponentColor::Cyan);
    if (section)
        if (auto fields = ui::BeginFields("skybox"))
        {
            changed |= ui::Field("Mode",
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

            switch (sky->mode_)
            {
                case Skybox_C::Mode::HDRI:
                    ui::Field("HDRI",
                              [&]
                              {
                                  const std::string name =
                                      ui::assetName(*app.ctx_->assetManager_, sky->hdri_);
                                  if (ui::AssetRow(ICON_MD_PANORAMA, ComponentColor::Cyan, name))
                                      assetPicker_.openHdri(ent, app.projectDir_);
                              });
                    break;

                case Skybox_C::Mode::FlatColor:
                    changed |= ui::Field(
                        "Color", [&] { return ui::ColorField("##skycolor", sky->color1_.data(), 3); });
                    break;

                case Skybox_C::Mode::Gradient:
                    changed |= ui::Field(
                        "Sky", [&] { return ui::ColorField("##skysky", sky->color1_.data(), 3); });
                    changed |= ui::Field("Horizon",
                                         [&] {
                                             return ui::ColorField("##skyhorizon",
                                                                   sky->color2_.data(), 3);
                                         });
                    changed |= ui::Field(
                        "Ground", [&] { return ui::ColorField("##skyground", sky->color3_.data(), 3); });
                    changed |= ui::FieldSlider("Width", &sky->horizonWidth_, 0.01f, 1.0f);
                    break;
            }

            changed |= ui::FieldDragFloat("Intensity", &sky->intensity_, 0.01f);
        }

    if (changed)
        app.world_->instances().markDirty<Skybox_C>(ent);
}

// Registry-declared components: one collapsing group per component, one row
// per field — no per-component editor code.
void InspectorPanel::drawReflected(EntityHandle ent, World& world, App& app)
{
    FieldUIContext fieldCtx{&app, &assetPicker_, ent, nullptr};

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
            if (auto fields = ui::BeginFields(t.name.c_str()))
                for (const Field& f : t.fields)
                    if (f.type->drawUI)
                    {
                        fieldCtx.component_ = &t;
                        changed |= ui::Field(prettyLabel(f.name).c_str(),
                                             [&] { return f.type->drawUI(f.ptrIn(c), f, fieldCtx); });
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

void InspectorPanel::drawAddComponent(EntityHandle ent, World& world)
{
    ImGui::Spacing();
    if (ui::AddButton(ICON_MD_ADD "  Add component"))
        ImGui::OpenPopup("##addComponent");

    if (!ImGui::BeginPopup("##addComponent"))
        return;

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
            // Same contract as the load path: some components rebuild derived
            // state after their fields exist.
            if (t.meta.onDeserialized)
                t.meta.onDeserialized(ent, world);
            world.instances().markDirty(ent, t.mask());
        }
    }
    ImGui::EndPopup();
}

}  // namespace batap
