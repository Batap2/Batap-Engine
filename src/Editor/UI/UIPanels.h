#pragma once

#include "Assets/AssetHandle.h"
#include "Components/EntityHandle.h"
#include "Context.h"


#include <memory>
#include <optional>

namespace batap
{
struct World;
struct App;

struct UIPanels
{
    void draw(World& world, App& app);
    void drawLeftPanel();
    void drawRegistryTree();

    std::optional<EntityHandle> _currentlyClickedSelectedEntity;

    void drawEntityMenu();

    World* world_ = nullptr;
    App* app_ = nullptr;

   private:
    void drawTransformMenu(EntityHandle ent);
    void drawMeshMenu(EntityHandle ent);
    void drawPointLightMenu(EntityHandle ent);
    void drawAssetSelectionPopup(EntityHandle selectedEntity, AssetType type);

    std::optional<EntityHandle> rotationEditEntity_;
    v3f rotationEditEulerDeg_ = v3f::Zero();
    quatf rotationEditSourceQuat_ = quatf::Identity();
};
}  // namespace batap
