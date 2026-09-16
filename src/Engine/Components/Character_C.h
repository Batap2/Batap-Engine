#pragma once

#include "EigenTypes.h"
#include "Reflection/ComponentRegistry.h"

#include <cstdint>

namespace batap
{

struct Character_C
{
    enum class Mode : uint32_t
    {
        Walk = 0,
        Fly = 1
    };

    Mode mode_ = Mode::Walk;

    float radius_ = 0.3f;
    float halfHeight_ = 0.6f;
    float maxSlopeDeg_ = 50.f;
    float mass_ = 70.f;
    float jumpSpeed_ = 4.f;

    v3f gravity_ = {0.f, -9.81f, 0.f};

    // In Walk the component along gravity is dropped and climbing goes through
    // wantJump_; in Fly this is the velocity, whole.
    v3f moveVelocity_ = v3f::Zero();
    bool wantJump_ = false;

    bool onGround_ = false;
    v3f velocity_ = v3f::Zero();

    // Raised by entt's on_update: whoever edits this component must go through
    // registry.patch or the character keeps its old shape.
    bool dirty_ = false;
};

static_assert(refl::fieldName<Character_C, 0>() == "mode");
static_assert(refl::fieldName<Character_C, 1>() == "radius");
static_assert(refl::fieldName<Character_C, 2>() == "halfHeight");
static_assert(refl::fieldName<Character_C, 3>() == "maxSlopeDeg");
static_assert(refl::fieldName<Character_C, 4>() == "mass");
static_assert(refl::fieldName<Character_C, 5>() == "jumpSpeed");
static_assert(refl::fieldName<Character_C, 6>() == "gravity");
static_assert(refl::fieldName<Character_C, 7>() == "moveVelocity");
static_assert(refl::fieldName<Character_C, 8>() == "wantJump");

BATAP_COMPONENT(Character_C, "character", fieldSkip<&Character_C::dirty_>(),
                fieldMeta<&Character_C::radius_>({.speed = 0.01f, .min = 0.01f, .max = 10.f}),
                fieldMeta<&Character_C::halfHeight_>({.speed = 0.01f, .min = 0.01f, .max = 10.f}),
                fieldMeta<&Character_C::maxSlopeDeg_>({.speed = 0.5f, .min = 0.f, .max = 89.f}),
                fieldMeta<&Character_C::mass_>({.speed = 0.5f, .min = 0.1f, .max = 10000.f}),
                fieldMeta<&Character_C::jumpSpeed_>({.speed = 0.1f, .min = 0.f, .max = 100.f}));

}  // namespace batap
