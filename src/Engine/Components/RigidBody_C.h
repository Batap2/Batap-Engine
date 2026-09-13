#pragma once

#include "EigenTypes.h"
#include "Reflection/ComponentRegistry.h"

#include <cstdint>
#include <vector>

namespace batap
{

inline constexpr uint32_t kInvalidBodyId = 0xffffffffu;

struct Shape
{
    enum class Kind : uint32_t
    {
        Box = 0,
        Sphere = 1,
        Capsule = 2
    };

    Kind kind_ = Kind::Box;

    // Only the dimensions its kind_ uses are read: halfExtents for a box,
    // radius for a sphere, radius + halfHeight for a capsule.
    v3f halfExtents_ = {0.5f, 0.5f, 0.5f};
    float radius_ = 0.5f;
    float halfHeight_ = 0.5f;

    v3f localPos_ = v3f::Zero();
    v3f localRotDeg_ = v3f::Zero();
};

struct RigidBody_C
{
    enum class Motion : uint32_t
    {
        Static = 0,
        Kinematic = 1,
        Dynamic = 2
    };

    bool active_ = true;
    Motion motion_ = Motion::Dynamic;

    std::vector<Shape> shapes_{Shape{}};

    float mass_ = 1.f;
    float friction_ = 0.2f;
    float restitution_ = 0.f;
    float linearDamping_ = 0.05f;
    float angularDamping_ = 0.05f;
    float gravityFactor_ = 1.f;

    uint32_t bodyId_ = kInvalidBodyId;
    v3f shapeScale_ = {1.f, 1.f, 1.f};
    // Raised by entt's on_update: whoever edits this component must go through
    // registry.patch (the inspector does) or the body keeps its old settings.
    bool dirty_ = false;
};

static_assert(refl::fieldName<RigidBody_C, 0>() == "active");
static_assert(refl::fieldName<RigidBody_C, 1>() == "motion");
static_assert(refl::fieldName<RigidBody_C, 2>() == "shapes");
static_assert(refl::fieldName<RigidBody_C, 3>() == "mass");
static_assert(refl::fieldName<RigidBody_C, 4>() == "friction");
static_assert(refl::fieldName<RigidBody_C, 5>() == "restitution");
static_assert(refl::fieldName<RigidBody_C, 6>() == "linearDamping");
static_assert(refl::fieldName<RigidBody_C, 7>() == "angularDamping");
static_assert(refl::fieldName<RigidBody_C, 8>() == "gravityFactor");

BATAP_COMPONENT(RigidBody_C, "rigidBody", fieldSkip<&RigidBody_C::bodyId_>(),
                fieldSkip<&RigidBody_C::shapeScale_>(),
                fieldSkip<&RigidBody_C::dirty_>(),
                fieldMeta<&RigidBody_C::mass_>({.speed = 0.05f, .min = 0.001f, .max = 10000.f}),
                fieldMeta<&RigidBody_C::friction_>({.speed = 0.01f, .min = 0.f, .max = 1.f}),
                fieldMeta<&RigidBody_C::restitution_>({.speed = 0.01f, .min = 0.f, .max = 1.f}),
                fieldMeta<&RigidBody_C::linearDamping_>({.speed = 0.01f, .min = 0.f, .max = 1.f}),
                fieldMeta<&RigidBody_C::angularDamping_>({.speed = 0.01f, .min = 0.f, .max = 1.f}),
                fieldMeta<&RigidBody_C::gravityFactor_>({.speed = 0.01f, .min = 0.f, .max = 10.f}));

}  // namespace batap
