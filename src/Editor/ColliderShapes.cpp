#include "ColliderShapes.h"

#include "Assets/AssetManager.h"
#include "Assets/Mesh.h"
#include "Components/Mesh_C.h"
#include "Engine.h"
#include "Systems/Hierarchy_S.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace batap::colliderShape
{
namespace
{
constexpr float kDegToRad = std::numbers::pi_v<float> / 180.f;
constexpr float kRadToDeg = 180.f / std::numbers::pi_v<float>;
constexpr float kMinSize = 0.001f;
}  // namespace

quatf localRot(const Shape& s)
{
    const v3f r = s.localRotDeg_ * kDegToRad;
    return (angleaxisf(r.x(), v3f::UnitX()) * angleaxisf(r.y(), v3f::UnitY()) *
            angleaxisf(r.z(), v3f::UnitZ()))
        .normalized();
}

v3f eulerDeg(const quatf& q)
{
    return q.normalized().toRotationMatrix().canonicalEulerAngles(0, 1, 2) * kRadToDeg;
}

Pose worldPose(const Transform_C& tc, const Shape& s)
{
    const v3f scale = tc.scale().cwiseAbs();
    const quatf rot = tc.worldRot().normalized();
    return {tc.worldPos() + rot * s.localPos_.cwiseProduct(scale), (rot * localRot(s)).normalized(),
            scale};
}

void setWorldPos(Shape& s, const Transform_C& tc, const v3f& pos)
{
    const v3f scale = tc.scale().cwiseAbs().cwiseMax(kMinSize);
    s.localPos_ = (tc.worldRot().conjugate() * (pos - tc.worldPos())).cwiseQuotient(scale);
}

void setWorldRot(Shape& s, const Transform_C& tc, const quatf& rot)
{
    s.localRotDeg_ = eulerDeg(tc.worldRot().conjugate() * rot);
}

v3f size(const Shape& s)
{
    switch (s.kind_)
    {
        case Shape::Kind::Sphere:
            return v3f::Constant(s.radius_);
        case Shape::Kind::Capsule:
            return {s.radius_, s.halfHeight_, s.radius_};
        case Shape::Kind::Mesh:
        case Shape::Kind::Hull:
            return v3f::Ones();
        case Shape::Kind::Box:
            break;
    }
    return s.halfExtents_;
}

void setSize(Shape& s, const v3f& size)
{
    const v3f v = size.cwiseMax(kMinSize);
    switch (s.kind_)
    {
        case Shape::Kind::Sphere:
            s.radius_ = v.mean();
            break;
        case Shape::Kind::Capsule:
            s.radius_ = (v.x() + v.z()) * 0.5f;
            s.halfHeight_ = v.y();
            break;
        case Shape::Kind::Mesh:
        case Shape::Kind::Hull:
            break;
        case Shape::Kind::Box:
            s.halfExtents_ = v;
            break;
    }
}

std::optional<AABB> meshBounds(Engine& ctx, EntityHandle body)
{
    const Transform_C* bodyTc = body.try_get<Transform_C>();
    if (!bodyTc)
        return std::nullopt;

    entt::registry& reg = *body.reg_;
    const transform toBody = bodyTc->world().inverse();
    AABB box;
    const auto visit = [&](const auto& self, entt::entity e) -> void
    {
        if (const auto* mc = reg.try_get<Mesh_C>(e); mc && mc->mesh_)
            if (const auto* tc = reg.try_get<Transform_C>(e))
                if (const Mesh* mesh = ctx.assetManager_->get(mc->mesh_);
                    mesh && mesh->localBounds_.valid())
                    box.extend(mesh->localBounds_.transformed(toBody * tc->world()));
        for (entt::entity child : Hierarchy_S::children({&reg, e}))
            self(self, child);
    };
    visit(visit, body.entity_);

    if (!box.valid())
        return std::nullopt;
    return box;
}

bool fit(Engine& ctx, EntityHandle body, Shape& s)
{
    const auto box = meshBounds(ctx, body);
    if (!box)
        return false;

    const v3f half = box->halfSize().cwiseMax(kMinSize);
    s.localPos_ = box->center();
    s.localRotDeg_ = v3f::Zero();
    switch (s.kind_)
    {
        case Shape::Kind::Box:
            s.halfExtents_ = half;
            break;
        case Shape::Kind::Sphere:
            s.radius_ = half.maxCoeff();
            break;
        case Shape::Kind::Capsule: {
            Eigen::Index axis = 1;
            half.maxCoeff(&axis);
            float radius = 0.f;
            for (Eigen::Index k = 0; k < 3; ++k)
                if (k != axis)
                    radius = std::max(radius, half[k]);
            s.radius_ = radius;
            s.halfHeight_ = std::max(half[axis] - radius, kMinSize);
            if (axis == 0)
                s.localRotDeg_ = {0.f, 0.f, 90.f};
            else if (axis == 2)
                s.localRotDeg_ = {90.f, 0.f, 0.f};
            break;
        }
        case Shape::Kind::Mesh:
        case Shape::Kind::Hull:
            s.localPos_ = v3f::Zero();
            if (const auto* mc = body.try_get<Mesh_C>(); mc && mc->mesh_)
                s.mesh_ = mc->mesh_;
            break;
    }
    return true;
}

const char* kindName(Shape::Kind k)
{
    switch (k)
    {
        case Shape::Kind::Sphere:
            return "Sphere";
        case Shape::Kind::Capsule:
            return "Capsule";
        case Shape::Kind::Mesh:
            return "Mesh";
        case Shape::Kind::Hull:
            return "Hull";
        case Shape::Kind::Box:
            break;
    }
    return "Box";
}
}  // namespace batap::colliderShape
