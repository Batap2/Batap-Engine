#pragma once

#include "Components/EntityHandle.h"
#include "Engine.h"
#include "Serialization/TemplateInstances.h"
#include "UI/AssetPickerPopup.h"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>

namespace batap
{
struct World;
struct App;
namespace ui
{
struct ComponentSection;
}

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

    void beginInstance(World& world, App& app, EntityHandle ent);
    void drawTemplateSection(App& app, EntityHandle ent);
    void markField(const char* label, const char* type, const char* field);
    bool sectionTemplated() const;
    bool sectionOverridden(const char* type) const;
    void sectionActions(const ui::ComponentSection& section, const char* type);
    void runAction(World& world, App& app, EntityHandle ent);

    struct InstanceContext
    {
        InstanceView view_;
        const InstanceMember* member_ = nullptr;
        nlohmann::json diff_;
        bool entryRoot_ = false;
    };
    std::optional<InstanceContext> instance_;

    enum struct Action
    {
        None,
        RevertField,
        ApplyField,
        RevertComponent,
        ApplyComponent,
        RevertAll,
        ApplyAll,
        Unlink,
        OpenTemplate,
        SelectRoot
    };
    Action action_ = Action::None;
    std::string actionType_;
    std::string actionField_;

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
