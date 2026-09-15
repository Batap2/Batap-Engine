#pragma once
#include <cstdint>
#include <entt/entt.hpp>
#include <vector>

#include "Components/EntityHandle.h"
#include "Components/Transform_C.h"
#include "EigenTypes.h"

namespace batap
{

struct World;
struct GPUInstanceManager;

struct Transform_S
{
    uint32_t flushEpoch_ = 1;
    std::vector<entt::entity> dirty_;

    bool update(entt::registry& reg, GPUInstanceManager& instanceManager);

    void setLocalPosition(EntityHandle e, const v3f& p);
    void setLocalRotation(EntityHandle e, const quatf& q);
    void setLocalScale(EntityHandle e, const v3f& s);

    void setPosition(EntityHandle e, const v3f& p, Space space = Space::World);
    void setRotation(EntityHandle e, const quatf& q, Space space = Space::World);

    void translate(EntityHandle e, const v3f& vec, Space space = Space::Local);
    void rotate(EntityHandle e, const quatf& delta, Space space = Space::Local);
    void rotate(EntityHandle e, const v3f& axis, float radians, Space space = Space::Local);
    void scale(EntityHandle e, const v3f& vec);

    bool flushDirty(entt::registry& reg, GPUInstanceManager& instanceManager);
    void markDirty(EntityHandle e);

   private:
    static entt::entity to_entity(const EntityHandle& h)
    {
        return h.valid() ? h.entity_ : entt::null;
    }

    static bool has_transform(const EntityHandle& h)
    {
        return h.valid() && h.reg_->any_of<Transform_C>(h.entity_);
    }

    static void ensure_chain_up_to_date(EntityHandle e, GPUInstanceManager& instanceManager);
};
}  // namespace batap
