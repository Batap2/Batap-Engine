#pragma once

#include "Bbox.h"
#include "Components/EntityHandle.h"
#include "Components/RigidBody_C.h"
#include "Components/Transform_C.h"
#include "EigenTypes.h"

#include <optional>

namespace batap
{
struct Engine;

namespace colliderShape
{
struct Pose
{
    v3f pos_ = v3f::Zero();
    quatf rot_ = quatf::Identity();
    v3f scale_ = v3f::Ones();
};

quatf localRot(const Shape& s);
v3f eulerDeg(const quatf& q);

Pose worldPose(const Transform_C& tc, const Shape& s);
void setWorldPos(Shape& s, const Transform_C& tc, const v3f& pos);
void setWorldRot(Shape& s, const Transform_C& tc, const quatf& rot);

v3f size(const Shape& s);
void setSize(Shape& s, const v3f& size);

std::optional<AABB> meshBounds(Engine& ctx, EntityHandle body);
bool fit(Engine& ctx, EntityHandle body, Shape& s);

const char* kindName(Shape::Kind k);
}  // namespace colliderShape
}  // namespace batap
