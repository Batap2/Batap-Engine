#include "InspectorPanel.h"

#include <array>
#include <filesystem>

#include "App.h"
#include "Assets/AssetManager.h"
#include "Assets/AssetSlotMap.h"
#include "Assets/Material.h"
#include "Assets/Texture.h"
#include "Components/Materials_C.h"
#include "Components/Mesh_C.h"
#include "Components/PointLight_C.h"
#include "Components/Skybox_C.h"
#include "Components/Transform_C.h"
#include "Systems/Systems.h"
#include "Systems/Transform_S.h"
#include "UI/AssetHolder.h"
#include "UI/CollapsingGroup.h"
#include "UI/Field.h"
#include "World.h"

#include <imgui.h>
#include <Eigen/Geometry>
#include <numbers>

namespace batap
{

void InspectorPanel::draw(World& world, App& app, EntityHandle ent)
{
    drawTransform(ent, world);
    drawMesh(ent, app);
    drawMaterials(ent, app);
    drawPointLight(ent, world);
    drawSkybox(ent, app);
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
                              world.systems_->_transforms->setLocalPosition(ent, pos);
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
                              world.systems_->_transforms->setLocalRotation(ent, newRot);
                          }
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
            ui::Field(
                "Mesh",
                [&]
                {
                    std::string meshLabel;
                    if (meshC->_mesh)
                        if (auto* p = app.ctx_->_assetManager->getPath(meshC->_mesh))
                            meshLabel = std::filesystem::path(*p).stem().string();
                    if (AssetHolder({.size_ = v2f(40, 40), ._thumbnail = meshC->_mesh ? 1ull : 0, .label_ = meshLabel}))
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

static std::string texLabelFromHeapIdx(AssetManager* am, uint32_t heapIdx)
{
    auto* white = am->get<Texture>(std::string("__default_white"));
    if (white && heapIdx == white->heapIdx_)
        return {};
    std::string result;
    am->getSlotMap<Texture>()->for_each(
        [&](TextureHandle, const AssetSlotMap<Texture>::Asset& a)
        {
            if (result.empty() && a.value_.heapIdx_ == heapIdx)
                result = std::filesystem::path(a.path_).stem().string();
        });
    return result;
}

void InspectorPanel::drawMaterials(EntityHandle ent, App& app)
{
    auto* mc = ent.try_get<Materials_C>();
    if (!mc || mc->count == 0)
        return;

    if (auto g = ui::CollapsingGroup("Materials"))
    {
        for (uint8_t i = 0; i < mc->count; ++i)
        {
            const std::string slotLabel = "Slot " + std::to_string(i);
            if (auto sg = ui::CollapsingGroup(slotLabel.c_str()))
            {
                if (auto f = ui::BeginFields(slotLabel.c_str()))
                {
                    ui::Field("Asset",
                              [&]
                              {
                                  std::string matLabel;
                                  if (mc->slots[i])
                                      if (auto* p = app.ctx_->_assetManager->getPath(mc->slots[i]))
                                          matLabel = std::filesystem::path(*p).stem().string();
                                  if (AssetHolder({.size_      = v2f(40, 40),
                                                   ._thumbnail = mc->slots[i] ? 1ull : 0,
                                                   .label_     = matLabel}))
                                      assetPicker_.open(ent, AssetType::Material,
                                                        app.projectDir_, i);
                                  return true;
                              });

                    const MaterialHandle handle = mc->slots[i];
                    if (handle)
                    {
                        auto* mat = app.ctx_->_assetManager->get<Material>(handle);
                        if (mat)
                        {
                            Material copy = *mat;
                            bool changed  = false;

                            changed |= ui::Field("Albedo",
                                                 [&]
                                                 {
                                                     ImGui::SetNextItemWidth(-1.0f);
                                                     return ImGui::ColorEdit4("##alb",
                                                                              copy.albedo);
                                                 });
                            changed |= ui::FieldDragFloat("Roughness", &copy.roughness, 0.01f,
                                                          0.f, 1.f);
                            changed |= ui::FieldDragFloat("Metallic", &copy.metallic, 0.01f,
                                                          0.f, 1.f);
                            changed |= ui::FieldDragFloat("Reflectivity", &copy.reflectivity,
                                                          0.01f, 0.f, 1.f);

                            if (changed)
                                app.ctx_->_assetManager->update(handle, copy);

                            static constexpr std::array<const char*, 4> kTexLabels = {
                                "Albedo Tex", "Normal Tex", "Roughness Tex", "Metallic Tex"};
                            const std::array<uint32_t, 4> matTexIdx = {
                                mat->albedoTexIdx_, mat->normalTexIdx_,
                                mat->roughnessTexIdx_, mat->metallicTexIdx_};
                            for (uint8_t ch = 0; ch < 4; ++ch)
                            {
                                ui::Field(kTexLabels[ch],
                                          [&, ch]
                                          {
                                              std::string lbl = texLabelFromHeapIdx(
                                                  app.ctx_->_assetManager.get(), matTexIdx[ch]);
                                              if (AssetHolder({.size_      = v2f(40, 40),
                                                               ._thumbnail = 0,
                                                               .label_     = lbl.empty() ? "None" : lbl}))
                                                  assetPicker_.open(handle, ch, app.projectDir_);
                                              return true;
                                          });
                            }
                        }
                    }
                }
            }
        }
        assetPicker_.draw(app);
    }
}

// -----------------------------------------------------------------------------

void InspectorPanel::drawPointLight(EntityHandle ent, World& world)
{
    auto* plC = ent.try_get<PointLight_C>();
    if (!plC)
        return;

    bool changed = false;

    if (auto g = ui::CollapsingGroup("Point Light"))
        if (auto _ = ui::BeginFields("pointlight"))
        {
            changed |= ui::FieldDragFloat3("Color", plC->color_.data(), 0.01f);
            changed |= ui::FieldDragFloat("Intensity", &plC->intensity_, 0.1f);
            changed |= ui::FieldDragFloat("Radius", &plC->radius_, 0.1f);
            changed |= ui::FieldDragFloat("Falloff", &plC->falloff_, 0.01f);
            changed |= ui::Field("Shadows",
                                 [&] { return ImGui::Checkbox("##castSh", &plC->castShadows_); });
        }

    if (changed)
        world.instanceManager_->markDirty(ent, ComponentFlag::PointLight);
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
            changed |= ui::Field("Mode",
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
                                  if (auto* p = app.ctx_->_assetManager->getPath(sky->hdri_))
                                      label = std::filesystem::path(*p).stem().string();
                              if (AssetHolder({.size_      = v2f(40, 40),
                                               ._thumbnail = sky->hdri_ ? 1ull : 0,
                                               .label_     = label.empty() ? "None" : label}))
                                  assetPicker_.openHdri(ent, app.projectDir_);
                              assetPicker_.draw(app);
                              return true;
                          });
            }
            else if (sky->mode_ == Skybox_C::Mode::FlatColor)
            {
                changed |= ui::Field("Color",
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
                changed |= ui::Field("Horizon",
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
                              return ImGui::SliderFloat("##skyhw", &sky->horizonWidth_, 0.01f, 1.0f);
                          });
            }

            changed |= ui::FieldDragFloat("Intensity", &sky->intensity_, 0.01f);
        }

    if (changed)
        app.world_->instanceManager_->markDirty(ent, ComponentFlag::Skybox);
}

}  // namespace batap
