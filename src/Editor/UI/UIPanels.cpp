#include "UIPanels.h"

#include "App.h"
#include "Assets/AssetManager.h"
#include "Components/EntityHandle.h"
#include "Components/Mesh_C.h"
#include "Components/Name_C.h"
#include "Components/PointLight_C.h"
#include "Components/Transform_C.h"
#include "Context.h"
#include "Instance/EntityFactory.h"
#include "Scene.h"
#include "Systems/Systems.h"
#include "Systems/TransformSystem.h"
#include "UI/AssetHolder.h"
#include "UI/IconsMaterialDesign.h"
#include "Utils/UIDGenerator.h"
#include "WindowsUtils/FileDialog.h"
#include "World.h"

#include <imgui_internal.h>
#include "imgui.h"

#include <Eigen/Geometry>
#include <algorithm>
#include <cstddef>
#include <memory>
#include <numbers>

using namespace ImGui;

namespace batap
{
void UIPanels::draw(World& world, App& app)
{
    world_ = &world;
    app_ = &app;
    drawLeftPanel();
}

void UIPanels::drawLeftPanel()
{
    ImGuiViewport* vp = ImGui::GetMainViewport();

    static float panelWidth = 150.0f;
    const float minWidth = 100.0f;
    const float maxWidth = 600.0f;
    const float resizeGrip = 6.0f;

    // --- fenêtre host fullscreen invisible ---
    ImGui::SetNextWindowPos(vp->Pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(vp->Size, ImGuiCond_Always);

    ImGuiWindowFlags hostFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                 ImGuiWindowFlags_NoBackground;

    ImGui::Begin("##LeftPanelHost", nullptr, hostFlags);

    // --- panel gauche ---
    ImGui::SetCursorScreenPos(vp->Pos);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.f, 0.f, 0.f, 0.70f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 10));

    ImGui::BeginChild("##LeftPanel", ImVec2(panelWidth, vp->Size.y), false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    if (Button("Import assets"))
    {
        OpenFilesDialogAsync({}, &app_->fileDialogMsgBus_);
    }

    Separator();
    drawRegistryTree();

    if (_currentlyClickedSelectedEntity)
    {
        Separator();
        drawEntityMenu();
    }

    ImGui::EndChild();

    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    ImVec2 gripPos(panelWidth - resizeGrip, 0);
    ImGui::SetCursorPos(gripPos);

    if (vp->Size.y != 0.0f)
    {
        ImGui::InvisibleButton("##LeftPanelResize", ImVec2(resizeGrip * 2.0f, vp->Size.y));
    }

    if (ImGui::IsItemHovered() || ImGui::IsItemActive())
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

    if (ImGui::IsItemActive())
    {
        panelWidth += ImGui::GetIO().MouseDelta.x;
        panelWidth = std::clamp(panelWidth, minWidth, maxWidth);
    }

    ImGui::End();
}

void UIPanels::drawRegistryTree()
{
    auto& reg = world_->scene_->_registry;

    if (ImGui::Button(ICON_MD_ADD))
    {
        ImGui::OpenPopup("AddEntityPopup");
    }

    if (ImGui::BeginPopup("AddEntityPopup"))
    {
        if (ImGui::MenuItem(ICON_MD_HVAC " Static Mesh"))
        {
            auto h = world_->entityFactory_->createStaticMesh(world_->scene_->_registry);
        }

        if (ImGui::MenuItem(ICON_MD_LIGHTBULB " Point Light"))
        {
            auto h = world_->entityFactory_->createPointLight(world_->scene_->_registry);
        }

        if (ImGui::MenuItem(ICON_MD_VIDEOCAM " Camera"))
        {
            // action
        }

        ImGui::EndPopup();
    }

    Separator();

    Text("Scene");

    int id = 0;
    for (auto e : reg.storage<entt::entity>())
    {
        PushID(id);

        EntityHandle h = {&reg, e};

        const char* icon = ICON_MD_CATEGORY;

        if (reg.all_of<Camera_C>(e))
            icon = ICON_MD_VIDEOCAM;
        else if (reg.all_of<Mesh_C>(e))
            icon = ICON_MD_HVAC;
        else if (reg.all_of<PointLight_C>(e))
            icon = ICON_MD_LIGHTBULB;

        TextUnformatted(icon);
        SameLine();

        bool selected = false;
        if (_currentlyClickedSelectedEntity)
        {
            selected = *_currentlyClickedSelectedEntity == h;
        }

        if (MenuItem(reg.get<Name_C>(e)._name.c_str(), nullptr, selected))
        {
            if (_currentlyClickedSelectedEntity)
            {
                _currentlyClickedSelectedEntity.reset();
            }
            _currentlyClickedSelectedEntity = h;
        }
        PopID();
        id++;
    }
}

void UIPanels::drawTransformMenu(EntityHandle ent)
{
    auto* t = ent.try_get<Transform_C>();
    if (!t)
        return;

    constexpr float RadToDeg = 180.0f / std::numbers::pi_v<float>;
    constexpr float DegToRad = std::numbers::pi_v<float> / 180.0f;

    Unindent(10);
    SeparatorText("Transform");
    Indent(10);

    v3f pos = t->pos();
    if (DragFloat3("Position", pos.data()))
    {
        world_->systems_->_transforms->setLocalPosition(ent, pos);
    }

    quatf rot = t->rot().normalized();

    // Réinitialiser le cache si on change d'entité
    if (!rotationEditEntity_.has_value() || *rotationEditEntity_ != ent)
    {
        rotationEditEntity_ = ent;
        rotationEditSourceQuat_ = rot;
        rotationEditEulerDeg_ = rot.toRotationMatrix().canonicalEulerAngles(0, 1, 2) * RadToDeg;
    }
    else
    {
        // Si la rotation a changé ailleurs que par cette UI, resync
        const float dot = std::abs(rotationEditSourceQuat_.dot(rot));
        if (1.0f - dot > 1e-4f)
        {
            rotationEditSourceQuat_ = rot;
            rotationEditEulerDeg_ = rot.toRotationMatrix().canonicalEulerAngles(0, 1, 2) * RadToDeg;
        }
    }

    if (DragFloat3("Rotation", rotationEditEulerDeg_.data(), 0.1f))
    {
        v3f eulerRad = rotationEditEulerDeg_ * DegToRad;

        quatf newRot = angleaxisf(eulerRad.x(), v3f::UnitX()) *
                       angleaxisf(eulerRad.y(), v3f::UnitY()) *
                       angleaxisf(eulerRad.z(), v3f::UnitZ());

        newRot.normalize();

        rotationEditSourceQuat_ = newRot;
        world_->systems_->_transforms->setLocalRotation(ent, newRot);
    }
}

void UIPanels::drawMeshMenu(EntityHandle ent)
{
    auto* meshC = ent.try_get<Mesh_C>();
    if (!meshC)
        return;

    Unindent(10);
    SeparatorText("Mesh");
    Indent(10);

    if (AssetHolder({.size_ = v2f(40, 40), ._thumbnail = meshC->_mesh ? 1ull : 0}))
    {
        OpenPopup("AssetPicker");
    }
    drawAssetSelectionPopup(ent, AssetType::Mesh);
}

void UIPanels::drawPointLightMenu(EntityHandle ent)
{
    auto* plC = ent.try_get<PointLight_C>();
    if (!plC)
        return;

    bool changed = false;

    Unindent(10);
    SeparatorText("Point Light");
    Indent(10);

    Text("Color : ");
    SameLine();
    if (DragFloat3("##col", plC->color_.data()))
        changed = true;

    Text("Intensity : ");
    SameLine();
    if (DragFloat("##intens", &plC->intensity_))
        changed = true;

    Text("Radius : ");
    SameLine();
    if (DragFloat("##radius", &plC->radius_))
        changed = true;

    Text("Falloff : ");
    SameLine();
    if (DragFloat("##fallof", &plC->falloff_))
        changed = true;

    Text("Cast shadows : ");
    SameLine();
    if (Checkbox("##castSh", &plC->castShadows_))
        changed = true;

    if (changed)
    {
        world_->instanceManager_->markDirty(ent, ComponentFlag::PointLight);
    }
}

void UIPanels::drawEntityMenu()
{
    auto& r = world_->scene_->_registry;
    if (!_currentlyClickedSelectedEntity)
        return;

    auto ent = *_currentlyClickedSelectedEntity;

    Indent(10);

    drawTransformMenu(ent);
    drawMeshMenu(ent);
    drawPointLightMenu(ent);

    Unindent(10);
}

void UIPanels::drawAssetSelectionPopup(EntityHandle ent, AssetType type)
{
    if (!ImGui::BeginPopup("AssetPicker"))
    {
        return;
    }

    // --- Header ---
    const char* title = (type == AssetType::Mesh)      ? "Select Mesh"
                        : (type == AssetType::Texture) ? "Select Texture"
                                                       : "Select Asset";

    ImGui::TextUnformatted(title);
    ImGui::Separator();

    // --- Search ---
    static char search[128] = "";
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##asset_search", "Search...", search, sizeof(search));

    // --- Actions row ---
    if (ImGui::Button("Clear"))
    {
        if (type == AssetType::Mesh)
        {
            if (auto* meshC = ent.try_get<Mesh_C>())
                meshC->_mesh = MeshHandle::null();
        }
        else if (type == AssetType::Texture)
        {
        }
    }
    ImGui::SameLine();

    if (ImGui::Button("Close"))
        ImGui::CloseCurrentPopup();

    ImGui::Separator();

    // --- List (scrollable) ---
    ImGui::BeginChild("##asset_list", ImVec2(0, 260), true);

    auto matches = [&](std::string_view name) -> bool
    {
        if (search[0] == '\0')
            return true;
        // Filtrage simple (case-sensitive). Si tu veux case-insensitive, je te le fais.
        return name.find(search) != std::string_view::npos;
    };

    // Helper pour afficher une entrée
    auto drawEntry = [&](auto handle, std::string_view name, auto assignFn)
    {
        if (!matches(name))
            return;

        // IMPORTANT: ne pas faire std::string(name).c_str() inline (temporaire)
        // On fabrique une string locale stable pour l'appel Selectable.
        std::string label(name);

        // Support double-clic: Selectable + IsItemHovered + IsMouseDoubleClicked
        bool selected = false;
        if (ImGui::Selectable(label.c_str(), &selected))
        {
            assignFn(handle);
            ImGui::CloseCurrentPopup();
        }

        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            assignFn(handle);
            ImGui::CloseCurrentPopup();
        }
    };

    app_->assetManager_->forEachAssetOfType(
        type,
        [&](AssetHandleAny hAny, std::string_view name, std::string_view path)
        {
            if (ImGui::Selectable(std::string(name).c_str()))
            {
                if (type == AssetType::Mesh)
                {
                    if (auto* meshC = ent.try_get<Mesh_C>())
                        meshC->_mesh = std::get<MeshHandle>(hAny);
                }
                else if (type == AssetType::Texture)
                {
                    // if (auto* matC = ent.try_get<Material_C>())
                    //     matC->_albedo = std::get<TextureHandle>(hAny);
                }

                ImGui::CloseCurrentPopup();
            }
        });

    ImGui::EndChild();
    ImGui::EndPopup();
}

}  // namespace batap
