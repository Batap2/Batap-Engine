#include "Renderer/ScatterDraws.h"

#include "Components/Camera_C.h"
#include "Components/Transform_C.h"

#include <algorithm>
#include <cmath>

namespace batap
{

uint64_t ScatterDraws::add(Batch batch)
{
    const uint64_t id = next_++;
    anchors_[id] = {batch.owner_, batch.centre_, batch.radius_, batch.castShadows_};
    ++ownerAnchors_[batch.owner_];
    fresh_.push_back(id);
    added_.emplace_back(id, std::move(batch));
    return id;
}

void ScatterDraws::remove(uint64_t batch)
{
    const auto anchor = anchors_.find(batch);
    if (anchor == anchors_.end())
        return;
    if (const auto owner = ownerAnchors_.find(anchor->second.owner_);
        owner != ownerAnchors_.end() && --owner->second == 0)
        ownerAnchors_.erase(owner);
    anchors_.erase(anchor);
    const auto it =
        std::find_if(added_.begin(), added_.end(), [&](const auto& a) { return a.first == batch; });
    if (it != added_.end())
        added_.erase(it);
    else
        removed_.push_back(batch);
}

void ScatterDraws::clear()
{
    for (const auto& [id, anchor] : anchors_)
        removed_.push_back(id);
    anchors_.clear();
    ownerAnchors_.clear();
    fresh_.clear();
    added_.clear();
}

void ScatterDraws::place(const entt::registry& reg, entt::entity camera, const ViewRect& view)
{
    if (!gpuDriven_)
        placed_.clear();
    owners_.clear();
    v3d cameraPos = v3d::Zero();
    v3d forward = v3d::Zero();
    double cone = 0.0;
    if (const Transform_C* tc = reg.valid(camera) ? reg.try_get<Transform_C>(camera) : nullptr)
    {
        cameraPos = tc->world().translation().cast<double>();
        const Camera_C* cc = reg.try_get<Camera_C>(camera);
        if (cc && !cc->ortho() && view.size_.y() > 0)
        {
            const double aspect = static_cast<double>(view.size_.x()) / view.size_.y();
            forward = -tc->world().linear().col(2).cast<double>().normalized();
            cone = std::atan(std::tan(0.5 * static_cast<double>(cc->fov_)) *
                             std::sqrt(1.0 + aspect * aspect));
        }
    }

    struct Cached
    {
        entt::entity entity_ = entt::null;
        bool placed_ = false;
        m3f linearF_ = m3f::Identity();
        m3d linear_ = m3d::Identity();
        m3d toOwnerAxes_ = m3d::Identity();
        v3d offset_ = v3d::Zero();
        double scale_ = 1.0;
    } owner;
    bool first = true;
    const auto cache = [&](entt::entity entity)
    {
        if (!first && entity == owner.entity_)
            return;
        first = false;
        owner.entity_ = entity;
        const Transform_C* tc = reg.valid(entity) ? reg.try_get<Transform_C>(entity) : nullptr;
        owner.placed_ = tc != nullptr;
        if (!tc)
            return;
        const transform& ownerWorld = tc->world();
        owner.linearF_ = ownerWorld.linear();
        owner.linear_ = owner.linearF_.cast<double>();
        owner.toOwnerAxes_ = owner.linear_.colwise().normalized().transpose() * owner.linear_;
        owner.offset_ = ownerWorld.translation().cast<double>() - cameraPos;
        owner.scale_ = owner.linear_.colwise().norm().maxCoeff();
        const auto ownerId = static_cast<uint32_t>(entt::to_entity(entity));
        if (std::ranges::none_of(owners_, [&](const Owner& o) { return o.owner_ == ownerId; }))
        {
            const m3d axes = owner.linear_.colwise().normalized();
            owners_.push_back(
                {ownerId, owner.linearF_, axes.cast<float>(), -(axes.transpose() * owner.offset_)});
        }
    };
    const auto emplace = [&](uint64_t id, const Anchor& anchor, bool visible)
    {
        Placement placement;
        placement.offset_ = (owner.linear_ * anchor.centre_ + owner.offset_).cast<float>();
        placement.radius_ = static_cast<float>(static_cast<double>(anchor.radius_) * owner.scale_);
        placement.visible_ = visible;
        placement.centreInOwner_ = owner.toOwnerAxes_ * anchor.centre_;
        placement.owner_ = static_cast<uint32_t>(entt::to_entity(anchor.owner_));
        placed_.emplace_back(id, placement);
    };

    if (gpuDriven_)
    {
        for (const auto& [entity, count] : ownerAnchors_)
            cache(entity);
        std::erase_if(fresh_,
                      [&](uint64_t id)
                      {
                          const auto it = anchors_.find(id);
                          if (it == anchors_.end())
                              return true;
                          cache(it->second.owner_);
                          if (!owner.placed_)
                              return false;
                          emplace(id, it->second, true);
                          return true;
                      });
        return;
    }
    fresh_.clear();
    for (const auto& [id, anchor] : anchors_)
    {
        cache(anchor.owner_);
        if (!owner.placed_)
            continue;
        const v3d offset = owner.linear_ * anchor.centre_ + owner.offset_;
        const double radius = static_cast<double>(anchor.radius_) * owner.scale_;
        const double distance = offset.norm();
        bool visible = true;
        if (cone > 0.0 && radius > 0.0 && distance > radius)
        {
            const double angle = std::acos(std::clamp(offset.dot(forward) / distance, -1.0, 1.0));
            visible = angle - std::asin(radius / distance) <= cone;
        }
        if (!visible && !anchor.castShadows_)
            continue;
        emplace(id, anchor, visible);
    }
}

}  // namespace batap
