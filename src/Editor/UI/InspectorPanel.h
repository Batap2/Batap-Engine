#pragma once

#include "Components/EntityHandle.h"
#include "Context.h"
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
    void drawTransform(EntityHandle ent, World& world);
    void drawMesh(EntityHandle ent, App& app);
    void drawMaterials(EntityHandle ent, App& app);
    void drawPointLight(EntityHandle ent, World& world);

    // Cache for euler rotation
    std::optional<EntityHandle> rotationEditEntity_;
    v3f   rotationEditEulerDeg_  = v3f::Zero();
    quatf rotationEditSourceQuat_ = quatf::Identity();

    AssetPickerPopup assetPicker_;
};
}  // namespace batap
