#include "InspectorPanel.h"

#include "UI/FieldUI.h"

#include <array>

#include "App.h"
#include "Assets/AssetManager.h"
#include "Assets/Mesh.h"
#include "Components/Camera_C.h"
#include "Components/FreeCamController_C.h"
#include "Components/Materials_C.h"
#include "Components/Mesh_C.h"
#include "Components/Name_C.h"
#include "Components/Skybox_C.h"
#include "Components/Transform_C.h"
#include "Components/UnknownComponents_C.h"
#include "Instance/InstanceDeclaration.h"
#include "Instance/Spawnable.h"
#include "Reflection/ComponentRegistry.h"
#include "Systems/FreeCamController_S.h"
#include "Systems/Hierarchy_S.h"
#include "Systems/Systems.h"
#include "Systems/Transform_S.h"
#include "UI/AssetRow.h"
#include "UI/ComponentSection.h"
#include "UI/Field.h"
#include "UI/IconsMaterialDesign.h"
#include "UI/Scoped.h"
#include "UI/UITheme.h"
#include "World.h"

#include <imgui.h>
#include <Eigen/Geometry>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <numbers>
#include <string_view>
#include <type_traits>

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

constexpr size_t kMaxMaterialSlots = std::extent_v<decltype(StaticMeshGPUData::materialIndices_)>;
}  // namespace

void InspectorPanel::draw(World& world, App& app, EntityHandle ent)
{
    beginInstance(world, app, ent);
    drawHeader(ent);
    drawTemplateSection(app, ent);
    drawTransform(world, ent);
    drawMesh(ent, app);
    drawMaterials(ent, app);
    drawSkybox(ent, app);
    drawReflected(ent, world, app);
    drawAddComponent(ent, world, app);

    // One picker serves every section above; OpenPopup and BeginPopup both run
    // inside this call, so they see the same ID stack whatever opened it.
    assetPicker_.draw(app);

    runAction(world, app, ent);
    instance_.reset();
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

void InspectorPanel::drawTransform(World& world, EntityHandle ent)
{
    auto* t = ent.try_get<Transform_C>();
    if (!t)
        return;

    ui::ComponentSection section("Transform", ComponentColor::Blue, false, nullptr,
                                 sectionTemplated(), sectionOverridden("transform"));
    sectionActions(section, "transform");
    if (!section)
        return;

    const bool parented = Hierarchy_S::hasParent(ent);
    const bool floating = world.origin_.anchor_ != entt::null || world.origin_.pos_ != v3d::Zero();
    TransformSpace space = space_;
    if (space == TransformSpace::Local && !parented)
        space = TransformSpace::Origin;
    if (space == TransformSpace::Origin && !floating)
        space = parented ? TransformSpace::World : TransformSpace::Local;
    if (space == TransformSpace::World && !parented && !floating)
        space = TransformSpace::Local;

    if (parented || floating)
    {
        struct Choice
        {
            TransformSpace space_;
            const char* label_;
            const char* tooltip_;
            bool offered_;
        };
        const std::array<Choice, 3> choices{{
            {TransformSpace::Local, ICON_MD_VIEW_IN_AR "  Local", "Relative to the parent.",
             parented},
            {TransformSpace::Origin, ICON_MD_MY_LOCATION "  Origin",
             "In world space, relative to the floating origin: what is simulated and drawn."
             " World scale is lossy, so it only shows.",
             floating},
            {TransformSpace::World, ICON_MD_PUBLIC "  World",
             "In world space, the floating origin added back, in double."
             " World scale is lossy, so it only shows.",
             true},
        }};
        const float count = 1.0f + (parented ? 1.0f : 0.0f) + (floating ? 1.0f : 0.0f);
        const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
        const float width = (ImGui::GetContentRegionAvail().x - spacing * (count - 1.0f)) / count;
        bool first = true;
        for (const Choice& c : choices)
        {
            if (!c.offered_)
                continue;
            if (!first)
                ImGui::SameLine(0.0f, spacing);
            first = false;
            const bool on = c.space_ == space;
            {
                ui::ScopedColor colors{{ImGuiCol_Button, on ? ui::accent : ui::bg2},
                                       {ImGuiCol_ButtonHovered, on ? ui::accentHover : ui::bg3},
                                       {ImGuiCol_Text, on ? ui::bg0 : ui::text}};
                if (ImGui::Button(c.label_, {width, 0.0f}))
                    space_ = c.space_;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", c.tooltip_);
        }
        ImGui::Spacing();
    }
    const bool inWorld = space != TransformSpace::Local;

    auto fields = ui::BeginFields("transform");
    if (!fields)
        return;

    if (space == TransformSpace::World)
    {
        v3d pos = world.origin_.pos_ + t->worldPos().cast<double>();
        if (ui::FieldDragDouble3("Position", pos.data(), 0.05f))
            ent.setPosition(v3f{(pos - world.origin_.pos_).cast<float>()}, Space::World);
        markField("Position", "transform", "pos");
    }
    else
    {
        v3f pos = inWorld ? t->worldPos() : t->pos();
        if (ui::FieldDragFloat3("Position", pos.data(), 0.05f))
        {
            if (inWorld)
                ent.setPosition(pos, Space::World);
            else
                ent.setLocalPosition(pos);
        }
        markField("Position", "transform", "pos");
    }

    // Euler angles are cached: a quaternion has several equivalent triplets,
    // and recomputing one every frame would make the field jump while dragged.
    const quatf rot = (inWorld ? t->worldRot() : t->rot()).normalized();
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
        if (inWorld)
            ent.setRotation(newRot, Space::World);
        else
            ent.setLocalRotation(newRot);
    }
    markField("Rotation", "transform", "rot");

    v3f scale = inWorld ? t->worldScale() : t->scale();
    ImGui::BeginDisabled(inWorld);
    if (ui::FieldDragFloat3("Scale", scale.data(), 0.01f) && !inWorld)
        ent.setLocalScale(scale);
    ImGui::EndDisabled();
    markField("Scale", "transform", "scale");
}

void InspectorPanel::drawMesh(EntityHandle ent, App& app)
{
    auto* meshC = ent.try_get<Mesh_C>();
    if (!meshC)
        return;

    const std::string name = ui::assetName(*app.ctx_->assetManager_, meshC->mesh_);

    ui::ComponentSection section("Mesh", ComponentColor::Violet, false, name.c_str(),
                                 sectionTemplated(), sectionOverridden("mesh"));
    sectionActions(section, "mesh");
    if (!section)
        return;

    if (auto fields = ui::BeginFields("mesh"))
    {
        ui::Field("Asset",
                  [&]
                  {
                      if (ui::AssetRow(AssetType::Mesh, name))
                          assetPicker_.open(ent, AssetType::Mesh, app.projectDir_);
                  });
        markField("Asset", "mesh", "mesh");
        ui::Field("Visible", [&] { return ImGui::Checkbox("##visible", &meshC->visible_); });
        markField("Visible", "mesh", "visible");
        ui::Field("Cast shadows",
                  [&] { return ImGui::Checkbox("##castShadows", &meshC->castShadows_); });
        markField("Cast shadows", "mesh", "castShadows");
        ui::Field("GI bounce", [&] { return ImGui::Checkbox("##giBounce", &meshC->giBounce_); });
        markField("GI bounce", "mesh", "giBounce");
    }
}

void InspectorPanel::drawMaterials(EntityHandle ent, App& app)
{
    auto* mc = ent.try_get<Materials_C>();
    if (!mc)
        return;

    const size_t slotCount = mc->slots.size();
    const std::string summary = std::to_string(slotCount) + (slotCount == 1 ? " slot" : " slots");

    ui::ComponentSection section("Materials", ComponentColor::Yellow, true, summary.c_str(),
                                 sectionTemplated(), sectionOverridden("materials"));
    sectionActions(section, "materials");
    if (section)
    {
        size_t removed = slotCount;
        if (auto fields = ui::BeginFields("materials"))
            for (size_t i = 0; i < slotCount; ++i)
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
                              const float removeW = ImGui::GetFrameHeight();
                              const float rowW = ImGui::GetContentRegionAvail().x - editW -
                                                 removeW - style.ItemSpacing.x * 2.f;
                              if (ui::AssetRow(AssetType::Material, name, rowW))
                                  assetPicker_.open(ent, AssetType::Material, app.projectDir_,
                                                    static_cast<uint8_t>(i));

                              ImGui::SameLine();
                              ImGui::BeginDisabled(!mc->slots[i]);
                              if (ImGui::Button("Edit"))
                                  app.uiPanels_.openMaterialEditor(mc->slots[i]);
                              ImGui::EndDisabled();

                              ImGui::SameLine();
                              ui::ScopedColor danger{{ImGuiCol_Button, ui::transparent},
                                                     {ImGuiCol_ButtonHovered, ui::red}};
                              if (ui::IconButton(ICON_MD_CLOSE, removeW))
                                  removed = i;
                          });
                markField(label.c_str(), "materials", "slots");
            }

        ImGui::BeginDisabled(slotCount >= kMaxMaterialSlots);
        if (ui::AddButton(ICON_MD_ADD "  Add slot"))
            mc->slots.emplace_back();
        ImGui::EndDisabled();

        if (removed < slotCount)
        {
            mc->slots.erase(mc->slots.begin() + static_cast<std::ptrdiff_t>(removed));
            app.world_->instances().markDirty<Materials_C>(ent);
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

    ui::ComponentSection section("Skybox", ComponentColor::Cyan, false, nullptr, sectionTemplated(),
                                 sectionOverridden("skybox"));
    sectionActions(section, "skybox");
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
            markField("Mode", "skybox", "mode");

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
                    markField("HDRI", "skybox", "hdri");
                    break;

                case Skybox_C::Mode::FlatColor:
                    changed |=
                        ui::Field("Color", [&]
                                  { return ui::ColorField("##skycolor", sky->color1_.data(), 3); });
                    markField("Color", "skybox", "color1");
                    break;

                case Skybox_C::Mode::Gradient:
                    changed |= ui::Field(
                        "Sky", [&] { return ui::ColorField("##skysky", sky->color1_.data(), 3); });
                    markField("Sky", "skybox", "color1");
                    changed |= ui::Field(
                        "Horizon",
                        [&] { return ui::ColorField("##skyhorizon", sky->color2_.data(), 3); });
                    markField("Horizon", "skybox", "color2");
                    changed |= ui::Field(
                        "Ground",
                        [&] { return ui::ColorField("##skyground", sky->color3_.data(), 3); });
                    markField("Ground", "skybox", "color3");
                    changed |= ui::FieldSlider("Width", &sky->horizonWidth_, 0.01f, 1.0f);
                    markField("Width", "skybox", "horizonWidth");
                    break;
            }

            changed |= ui::FieldDragFloat("Intensity", &sky->intensity_, 0.01f);
            markField("Intensity", "skybox", "intensity");
            changed |= ui::FieldDragFloat("Stars", &sky->starIntensity_, 1.f, 0.f, 1e6f);
            markField("Stars", "skybox", "starIntensity");
            changed |= ui::FieldDragFloat("Star count", &sky->starCount_, 100.f, 0.f, 50000.f);
            markField("Star count", "skybox", "starCount");
            changed |= ui::FieldDragFloat("Milky Way", &sky->milkyWayIntensity_, 0.1f, 0.f, 1e6f);
            markField("Milky Way", "skybox", "milkyWayIntensity");
            changed |= ui::FieldDragFloat3("Milky Way pole", sky->milkyWayPole_.data(), 0.01f);
            markField("Milky Way pole", "skybox", "milkyWayPole");
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
        ui::ComponentSection section(prettyLabel(t.name).c_str(), t.meta.color, removable, nullptr,
                                     sectionTemplated(), sectionOverridden(t.name.c_str()));
        sectionActions(section, t.name.c_str());
        if (section)
        {
            if (auto fields = ui::BeginFields(t.name.c_str()))
                for (const Field& f : t.fields)
                    if (f.type->drawUI)
                    {
                        fieldCtx.component_ = &t;
                        const std::string label = prettyLabel(f.name);
                        changed |= ui::Field(label.c_str(), [&]
                                             { return f.type->drawUI(f.ptrIn(c), f, fieldCtx); });
                        markField(label.c_str(), t.name.c_str(), f.name.c_str());
                    }

            if (t.mask() == componentMask<Camera_C>())
                drawCameraActions(ent, app);
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

void InspectorPanel::drawCameraActions(EntityHandle ent, App& app)
{
    EntityHandle editorCam = app.editorCamera();
    if (!editorCam.valid() || editorCam == ent)
        return;

    ImGui::Spacing();
    const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
    const float width = (ImGui::GetContentRegionAvail().x - spacing) * 0.5f;
    if (ImGui::Button(ICON_MD_VIDEOCAM "  Align to editor view", {width, 0.0f}))
    {
        const Transform_C& view = editorCam.get<Transform_C>();
        ent.setPosition(view.worldPos(), Space::World);
        ent.setRotation(view.worldRot(), Space::World);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Moves this camera to the editor view.");

    ImGui::SameLine(0.0f, spacing);
    if (ImGui::Button(ICON_MD_VISIBILITY "  View from camera", {width, 0.0f}))
    {
        const Transform_C& cam = ent.get<Transform_C>();
        // The free cam rebuilds its rotation from yaw/pitch every frame.
        auto& ctrl = editorCam.get<FreeCamController_C>();
        setFreeCamFrame(ctrl, ctrl.frame_, cam.worldRot());
        editorCam.setPosition(cam.worldPos(), Space::World);
        editorCam.setRotation(freeCamRotation(ctrl), Space::World);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Moves the editor view to this camera.");
}

void InspectorPanel::drawAddComponent(EntityHandle ent, World& world, App& app)
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

        if (!ImGui::MenuItem(prettyLabel(t.name).c_str()))
            continue;
        if (!t.getOrEmplace(*ent.reg_, ent.entity_))
        {
            app.showToast(t.refusal(*ent.reg_, ent.entity_));
            continue;
        }
        // Same contract as the load path: some components rebuild derived
        // state after their fields exist.
        if (t.meta.onDeserialized)
            t.meta.onDeserialized(ent, world);
        world.instances().markDirty(ent, t.mask());
    }
    ImGui::EndPopup();
}

void InspectorPanel::beginInstance(World& world, App& app, EntityHandle ent)
{
    instance_.reset();
    auto& reg = *ent.reg_;
    const entt::entity root = instanceRootOf(reg, ent.entity_);
    if (root == entt::null)
        return;
    InstanceContext ic;
    ic.view_ = viewInstance(world, *app.ctx_, root);
    ic.entryRoot_ = root == ent.entity_;
    ic.member_ = ic.view_.member(ent.entity_);
    if (ic.member_)
        ic.diff_ = diffMember(world, *app.ctx_, ic.view_, *ic.member_);
    instance_.emplace(std::move(ic));
}

bool InspectorPanel::sectionTemplated() const
{
    return instance_ && instance_->member_ != nullptr;
}

bool InspectorPanel::sectionOverridden(const char* type) const
{
    return sectionTemplated() && overridesComponent(instance_->diff_, type);
}

void InspectorPanel::sectionActions(const ui::ComponentSection& section, const char* type)
{
    if (section.revertClicked())
        action_ = Action::RevertComponent;
    else if (section.applyClicked())
        action_ = Action::ApplyComponent;
    else
        return;
    actionType_ = type;
    actionField_.clear();
}

void InspectorPanel::markField(const char* label, const char* type, const char* field)
{
    if (!sectionTemplated() || !overridesField(instance_->diff_, type, field))
        return;
    ImVec4 tint = ui::accent;
    tint.w = 0.25f;
    ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, ImGui::GetColorU32(tint), 0);

    ui::ScopedID id{label};
    const bool hovered = ImGui::GetStateStorage()->GetBool(ImGui::GetID(ui::FieldLabelHoverKey));
    if (!ui::BeginFieldMenu(hovered))
        return;
    if (ImGui::MenuItem("Revert"))
        action_ = Action::RevertField;
    if (ImGui::MenuItem("Apply to template"))
        action_ = Action::ApplyField;
    if (action_ == Action::RevertField || action_ == Action::ApplyField)
    {
        actionType_ = type;
        actionField_ = field;
    }
    ImGui::EndPopup();
}

void InspectorPanel::drawTemplateSection(App& app, EntityHandle ent)
{
    if (!instance_)
        return;
    const InstanceView& view = instance_->view_;
    ui::ComponentSection section("Template", ComponentColor::Cyan);
    if (!section)
        return;

    const std::string stem = std::filesystem::path(view.path_).stem().string();
    if (auto fields = ui::BeginFields("templateInstance"))
    {
        ui::Field("Template",
                  [&]
                  {
                      if (ui::AssetField(ICON_MD_WIDGETS, stem.c_str(), ui::accent))
                          action_ = Action::OpenTemplate;
                  });
        if (!instance_->entryRoot_)
        {
            auto& reg = *ent.reg_;
            const Spawnable& kind = spawnableFor(reg, view.root_);
            const std::string rootName = reg.get<Name_C>(view.root_).name_;
            ui::Field("Instance",
                      [&]
                      {
                          if (ui::AssetField(kind.icon, rootName.c_str(), ui::colorOfKind(kind.id)))
                              action_ = Action::SelectRoot;
                      });
        }
        if (!view.flat_.linkable_)
            ui::Field("Status",
                      [&] { ImGui::TextColored(ui::red, "%s", view.flat_.why_.c_str()); });
        else if (!instance_->member_)
            ui::Field("Status", [&] { ImGui::TextDisabled("Added to the instance"); });
    }

    if (!instance_->entryRoot_ || !view.flat_.linkable_)
        return;

    ImGui::Spacing();
    const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
    const float width = (ImGui::GetContentRegionAvail().x - spacing * 2.0f) / 3.0f;
    if (ImGui::Button(ICON_MD_UNDO "  Revert all", {width, 0.0f}))
        action_ = Action::RevertAll;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Takes every value back from the template. The position stays.");
    ImGui::SameLine(0.0f, spacing);
    if (ImGui::Button(ICON_MD_UPLOAD "  Apply all", {width, 0.0f}))
        action_ = Action::ApplyAll;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Writes the overrides into the template file.");
    ImGui::SameLine(0.0f, spacing);
    if (ImGui::Button(ICON_MD_LINK_OFF "  Unlink", {width, 0.0f}))
        action_ = Action::Unlink;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Turns the instance into plain entities.");
}

void InspectorPanel::runAction(World& world, App& app, EntityHandle ent)
{
    const Action action = action_;
    action_ = Action::None;
    if (action == Action::None || !instance_)
        return;

    const InstanceView& view = instance_->view_;
    const InstanceMember* member = instance_->member_;
    const Engine& ctx = *app.ctx_;
    const ComponentType* ct = ComponentRegistry::instance().find(actionType_);
    const Field* field = nullptr;
    if (ct)
        for (const Field& f : ct->fields)
            if (f.name == actionField_)
                field = &f;

    const auto apply = [&](const ApplyFilter& keep)
    {
        std::string why;
        const int skipped = applyToTemplate(world, ctx, view, keep, &why);
        if (skipped < 0)
            app.showToast(why);
        else if (skipped > 0)
            app.showToast(std::to_string(skipped) +
                          " override(s) sit in a nested template and were not applied");
        else
            app.showToast("Template updated");
    };

    switch (action)
    {
        case Action::None:
            break;
        case Action::OpenTemplate:
            app.uiPanels_.requestTemplate(
                app,
                std::filesystem::path(templateAbsPath(ctx, view.path_)).make_preferred().string());
            break;
        case Action::SelectRoot:
            app.uiPanels_.select({ent.reg_, view.root_});
            break;
        case Action::RevertField:
            if (member && ct && field)
                revertField(world, ctx, view, *member, *ct, *field);
            break;
        case Action::ApplyField:
            if (member && ct && field)
                apply([&](const InstanceMember& m, std::string_view type, std::string_view f)
                      { return &m == member && type == actionType_ && f == actionField_; });
            break;
        case Action::RevertComponent:
            if (member && ct)
                revertComponent(world, ctx, view, *member, *ct);
            break;
        case Action::ApplyComponent:
            if (member && ct)
                apply([&](const InstanceMember& m, std::string_view type, std::string_view)
                      { return &m == member && type == actionType_; });
            break;
        case Action::RevertAll:
            revertInstance(world, ctx, view, true);
            break;
        case Action::ApplyAll:
            apply([](const InstanceMember&, std::string_view, std::string_view) { return true; });
            break;
        case Action::Unlink:
            unlinkInstance(world, view.root_);
            break;
    }
}

}  // namespace batap
