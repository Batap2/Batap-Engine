#pragma once

#include "Components/EntityHandle.h"
#include "Engine.h"
#include "UI/AssetPickerPopup.h"

#include <optional>

namespace batap
{
struct World;
struct App;

struct InspectorPanel
{
    void draw(World& world, App& app, EntityHandle ent);

   private:
    void drawHeader(EntityHandle ent);
    void drawTransform(World& world, EntityHandle ent);
    void drawMesh(EntityHandle ent, App& app);
    void drawMaterials(EntityHandle ent, App& app);
    void drawSkybox(EntityHandle ent, App& app);
    void drawReflected(EntityHandle ent, World& world, App& app);
    void drawCameraActions(EntityHandle ent, App& app);
    // add menu: registry components minus markers and those already present
    void drawAddComponent(EntityHandle ent, World& world, App& app);

    enum struct TransformSpace
    {
        Local,
        Origin,
        World
    };
    TransformSpace space_ = TransformSpace::Local;

    // Cache for euler rotation
    std::optional<EntityHandle> rotationEditEntity_;
    v3f rotationEditEulerDeg_ = v3f::Zero();
    quatf rotationEditSourceQuat_ = quatf::Identity();

    AssetPickerPopup assetPicker_;
};
}  // namespace batap
