#include "InspectorPanel.h"

#include "App.h"
#include "Components/Mesh_C.h"
#include "Components/PointLight_C.h"
#include "Components/Transform_C.h"
#include "Systems/Systems.h"
#include "Systems/Transform_S.h"
#include "UI/AssetHolder.h"
#include "UI/PropRow.h"
#include "World.h"

#include <Eigen/Geometry>
#include <imgui.h>
#include <numbers>

namespace batap
{

void InspectorPanel::draw(World& world, App& app, EntityHandle ent)
{
    drawTransform(ent, world);
    drawMesh(ent, app);
    drawPointLight(ent, world);
}

// -----------------------------------------------------------------------------

void InspectorPanel::drawTransform(EntityHandle ent, World& world)
{
    if (!ent.try_get<Transform_C>())
        return;

    if (!ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
        return;

    auto* t = ent.try_get<Transform_C>();
    ImGui::Indent(8.0f);

    // Position
    v3f pos = t->pos();
    ui::PropRow("Position", [&]
    {
        if (ImGui::DragFloat3("##pos", pos.data(), 0.05f))
            world.systems_->_transforms->setLocalPosition(ent, pos);
    });

    // Rotation (euler avec cache pour éviter les flips)
    constexpr float kRadToDeg = 180.0f / std::numbers::pi_v<float>;
    constexpr float kDegToRad = std::numbers::pi_v<float> / 180.0f;

    quatf rot = t->rot().normalized();

    if (!rotationEditEntity_.has_value() || *rotationEditEntity_ != ent)
    {
        rotationEditEntity_   = ent;
        rotationEditSourceQuat_ = rot;
        rotationEditEulerDeg_ = rot.toRotationMatrix().canonicalEulerAngles(0, 1, 2) * kRadToDeg;
    }
    else if (1.0f - std::abs(rotationEditSourceQuat_.dot(rot)) > 1e-4f)
    {
        rotationEditSourceQuat_ = rot;
        rotationEditEulerDeg_ = rot.toRotationMatrix().canonicalEulerAngles(0, 1, 2) * kRadToDeg;
    }

    ui::PropRow("Rotation", [&]
    {
        if (ImGui::DragFloat3("##rot", rotationEditEulerDeg_.data(), 0.1f))
        {
            v3f eulerRad = rotationEditEulerDeg_ * kDegToRad;
            quatf newRot = angleaxisf(eulerRad.x(), v3f::UnitX()) *
                           angleaxisf(eulerRad.y(), v3f::UnitY()) *
                           angleaxisf(eulerRad.z(), v3f::UnitZ());
            newRot.normalize();
            rotationEditSourceQuat_ = newRot;
            world.systems_->_transforms->setLocalRotation(ent, newRot);
        }
    });

    ImGui::Unindent(8.0f);
}

// -----------------------------------------------------------------------------

void InspectorPanel::drawMesh(EntityHandle ent, App& app)
{
    if (!ent.try_get<Mesh_C>())
        return;

    if (!ImGui::CollapsingHeader("Mesh", ImGuiTreeNodeFlags_DefaultOpen))
        return;

    auto* meshC = ent.try_get<Mesh_C>();
    ImGui::Indent(8.0f);

    if (AssetHolder({.size_ = v2f(40, 40), ._thumbnail = meshC->_mesh ? 1ull : 0}))
        assetPicker_.open(ent, AssetType::Mesh);

    assetPicker_.draw(app);

    ImGui::Unindent(8.0f);
}

// -----------------------------------------------------------------------------

void InspectorPanel::drawPointLight(EntityHandle ent, World& world)
{
    auto* plC = ent.try_get<PointLight_C>();
    if (!plC)
        return;

    if (!ImGui::CollapsingHeader("Point Light", ImGuiTreeNodeFlags_DefaultOpen))
        return;

    ImGui::Indent(8.0f);

    bool changed = false;

    changed |= ui::PropFloat3("Color",     plC->color_.data(), 0.01f);
    changed |= ui::PropFloat ("Intensity", &plC->intensity_,   0.1f);
    changed |= ui::PropFloat ("Radius",    &plC->radius_,      0.1f);
    changed |= ui::PropFloat ("Falloff",   &plC->falloff_,     0.01f);
    changed |= ui::PropRow   ("Shadows",   [&]{ return ImGui::Checkbox("##castSh", &plC->castShadows_); });

    if (changed)
        world.instanceManager_->markDirty(ent, ComponentFlag::PointLight);

    ImGui::Unindent(8.0f);
}

}  // namespace batap
