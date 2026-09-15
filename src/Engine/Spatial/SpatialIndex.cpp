#include "Spatial/SpatialIndex.h"

#include "Assets/AssetManager.h"
#include "Assets/Mesh.h"
#include "Components/Billboard_C.h"
#include "Components/Camera_C.h"
#include "Components/Mesh_C.h"
#include "Components/Transform_C.h"
#include "Engine.h"
#include "Renderer/Billboards.h"
#include "World.h"

#include <cmath>

namespace batap
{

void SpatialIndex::connectHooks(entt::registry& reg)
{
    reg.on_construct<Mesh_C>().connect<&SpatialIndex::onMeshChanged>(*this);
    reg.on_update<Mesh_C>().connect<&SpatialIndex::onMeshChanged>(*this);
    reg.on_destroy<Mesh_C>().connect<&SpatialIndex::onMeshChanged>(*this);
}

namespace
{
entt::entity activeCameraOf(entt::registry& reg)
{
    entt::entity cam = entt::null;
    reg.view<Camera_C, Transform_C>().each(
        [&](entt::entity e, Camera_C& c, Transform_C&)
        {
            if (c.active_)
                cam = e;
        });
    return cam;
}

std::optional<BillboardQuad> quadOf(entt::registry& reg, entt::entity e, const CameraBasis& cam)
{
    const auto* bb = reg.try_get<Billboard_C>(e);
    const auto* tc = reg.try_get<Transform_C>(e);
    if (!bb || !tc)
        return std::nullopt;

    quatf rot = quatf::Identity();
    if (bb->orientation_ == Billboards::Orientation::None)
    {
        m3f basis = tc->world().linear();
        basis.colwise().normalize();
        rot = quatf(basis);
    }

    return billboardQuad(tc->world().translation(), rot, v2f(bb->width_, bb->height_),
                         bb->sizeMode_, bb->orientation_, cam);
}
}  // namespace

std::optional<CameraBasis> activeCameraBasis(entt::registry& reg)
{
    const entt::entity cam = activeCameraOf(reg);
    if (cam == entt::null)
        return std::nullopt;

    const transform& world = reg.get<Transform_C>(cam).world();
    return CameraBasis{world.translation(), world.linear().col(0).normalized(),
                       world.linear().col(1).normalized(), reg.get<Camera_C>(cam).fov_};
}

void SpatialIndex::refresh(World& world, Engine& ctx)
{
    world_ = &world;
    ctx_ = &ctx;

    if (!dirty_)
        return;
    dirty_ = false;

    entities_.clear();
    bounds_.clear();

    auto& assetManager = *ctx.assetManager_;
    for (auto [e, meshC, tc] : world.registry_.view<Mesh_C, Transform_C>().each())
    {
        if (!meshC.mesh_)
            continue;
        const Mesh* mesh = assetManager.get(meshC.mesh_);
        if (!mesh || !mesh->localBounds_.valid())
            continue;

        entities_.push_back(e);
        bounds_.push_back(mesh->localBounds_.transformed(tc.world()));
    }

    bvh_.build(bounds_);
}

RayHit SpatialIndex::raycast(const Ray& ray) const
{
    RayHit best;
    float bestT = ray.tMax_;
    const v3f invDir = ray.dir_.cwiseInverse();

    bvh_.raycast(ray,
                 [&](uint32_t prim, float tMax)
                 {
                     const AABBHit h =
                         rayAABB(ray.origin_, invDir, ray.tMin_, tMax, bounds_[prim]);
                     if (!h.hit_)
                         return tMax;

                     const bool inside = h.entryAxis_ < 0;
                     const float t = inside ? h.tFar_ : h.tNear_;
                     const int axis = inside ? h.exitAxis_ : h.entryAxis_;
                     if (t >= bestT)
                         return tMax;

                     bestT = t;
                     best.entity_ = entities_[prim];
                     best.t_ = t;
                     best.point_ = ray.origin_ + ray.dir_ * t;
                     best.normal_ = aabbFaceNormal(axis, ray.dir_, !inside);
                     return bestT;
                 });

    const RayHit billboard = raycastBillboards(ray, best.hit() ? best.t_ : ray.tMax_);
    return billboard.hit() ? billboard : best;
}

RayHit SpatialIndex::raycastBillboards(const Ray& ray, float bestT) const
{
    RayHit best;
    if (!world_)
        return best;

    auto& reg = world_->registry_;
    const auto cam = activeCameraBasis(reg);
    if (!cam)
        return best;

    for (auto [e, bb, tc] : reg.view<Billboard_C, Transform_C>().each())
    {
        const auto quad = quadOf(reg, e, *cam);
        if (!quad)
            continue;

        const QuadHit hit = rayQuad(ray, *quad, bestT);
        if (!hit.hit_)
            continue;

        bestT = hit.t_;
        best.entity_ = e;
        best.t_ = hit.t_;
        best.point_ = hit.point_;
        best.normal_ = hit.normal_;
    }

    return best;
}

void SpatialIndex::overlap(const AABB& box, std::vector<entt::entity>& out) const
{
    bvh_.overlap(box,
                 [&](uint32_t prim)
                 {
                     if (bounds_[prim].intersects(box))
                         out.push_back(entities_[prim]);
                 });
}

void SpatialIndex::overlap(const v3f& center, float radius, std::vector<entt::entity>& out) const
{
    const AABB box{center - v3f::Constant(radius), center + v3f::Constant(radius)};
    const float r2 = radius * radius;

    bvh_.overlap(box,
                 [&](uint32_t prim)
                 {
                     const AABB& b = bounds_[prim];
                     const v3f closest = center.cwiseMax(b.min_).cwiseMin(b.max_);
                     if ((closest - center).squaredNorm() <= r2)
                         out.push_back(entities_[prim]);
                 });
}

Ray rayFromScreen(World& world, Engine& ctx, const v2f& screenPos)
{
    auto& reg = world.registry_;

    const entt::entity cam = activeCameraOf(reg);
    if (cam == entt::null)
        return {};

    const v2i frameSize = ctx.getFrameSize();
    if (frameSize.x() <= 0 || frameSize.y() <= 0)
        return {};

    const Camera_C& camC = reg.get<Camera_C>(cam);
    const Transform_C& camT = reg.get<Transform_C>(cam);

    const float aspect = static_cast<float>(frameSize.x()) / static_cast<float>(frameSize.y());
    const m4f invViewProj = (camC.make_proj(aspect) * camC.make_view(camT.world())).inverse();

    // The scene is drawn through a negative-height viewport, so NDC +Y is the
    // top of the screen.
    const float ndcX = 2.f * screenPos.x() / static_cast<float>(frameSize.x()) - 1.f;
    const float ndcY = 1.f - 2.f * screenPos.y() / static_cast<float>(frameSize.y());

    const auto unproject = [&](float ndcZ)
    {
        const v4f p = invViewProj * v4f(ndcX, ndcY, ndcZ, 1.f);
        return v3f(p.head<3>() / p.w());
    };

    const v3f nearPoint = unproject(0.f);
    const v3f farPoint = unproject(1.f);
    const v3f delta = farPoint - nearPoint;
    const float length = delta.norm();
    if (length <= 0.f)
        return {};

    return Ray{nearPoint, delta / length, 0.f, length};
}

Ray rayFromScreen(World& world, Engine& ctx, const v2i& screenPos)
{
    return rayFromScreen(world, ctx,
                         v2f(static_cast<float>(screenPos.x()), static_cast<float>(screenPos.y())));
}

std::optional<v2f> ScreenProjector::project(const v3f& world) const
{
    const v4f clip = viewProj_ * v4f(world.x(), world.y(), world.z(), 1.f);
    if (clip.w() <= 1e-5f)
        return std::nullopt;

    const v2f ndc = clip.head<2>() / clip.w();
    return v2f((ndc.x() * 0.5f + 0.5f) * frameSize_.x(),
               (0.5f - ndc.y() * 0.5f) * frameSize_.y());
}

std::optional<ScreenProjector> screenProjector(World& world, Engine& ctx)
{
    auto& reg = world.registry_;

    const entt::entity cam = activeCameraOf(reg);
    if (cam == entt::null)
        return std::nullopt;

    const v2i frameSize = ctx.getFrameSize();
    if (frameSize.x() <= 0 || frameSize.y() <= 0)
        return std::nullopt;

    const Camera_C& camC = reg.get<Camera_C>(cam);
    const Transform_C& camT = reg.get<Transform_C>(cam);
    const float aspect = static_cast<float>(frameSize.x()) / static_cast<float>(frameSize.y());

    ScreenProjector out;
    out.viewProj_ = camC.make_proj(aspect) * camC.make_view(camT.world());
    out.frameSize_ = v2f(static_cast<float>(frameSize.x()), static_cast<float>(frameSize.y()));
    out.camPos_ = camT.world().translation();
    out.camRight_ = camT.world().linear().col(0).normalized();
    out.camUp_ = camT.world().linear().col(1).normalized();
    return out;
}

std::optional<AABB> entityBounds(World& world, Engine& ctx, entt::entity e)
{
    auto& reg = world.registry_;
    if (!reg.valid(e))
        return std::nullopt;

    const auto* tc = reg.try_get<Transform_C>(e);
    if (!tc)
        return std::nullopt;

    if (const auto* meshC = reg.try_get<Mesh_C>(e); meshC && meshC->mesh_)
        if (const Mesh* mesh = ctx.assetManager_->get(meshC->mesh_);
            mesh && mesh->localBounds_.valid())
            return mesh->localBounds_.transformed(tc->world());

    if (const auto cam = activeCameraBasis(reg))
        if (const auto quad = quadOf(reg, e, *cam))
        {
            AABB out;
            for (const float u : {-1.f, 1.f})
                for (const float v : {-1.f, 1.f})
                    out.extend(quad->corner(u, v));
            return out;
        }

    return std::nullopt;
}

}  // namespace batap
