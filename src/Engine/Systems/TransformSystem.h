#pragma once
#include <cstdint>
#include <entt/entt.hpp>
#include <vector>

#include "Components/EntityHandle.h"
#include "Components/Transform_C.h"
#include "Context.h"
#include "EigenTypes.h"

namespace batap
{

struct World;

enum class Space
{
    Local,
    Parent,
    World
};

struct TransformSystem
{
    uint32_t frameCount = 1;
    std::vector<entt::entity> dirty;

    void update(entt::registry& reg, GPUInstanceManager& instanceManager);

    void setLocalPosition(EntityHandle e, const v3f& p);
    void setLocalRotation(EntityHandle e, const quatf& q);
    void setLocalScale(EntityHandle e, const v3f& s);

    void translate(EntityHandle e, const v3f& vec, Space space = Space::Local);
    void rotate(EntityHandle e, const quatf& delta, Space space = Space::Local);
    void rotate(EntityHandle e, const v3f& axis, float radians, Space space = Space::Local);
    void scale(EntityHandle e, const v3f& vec);

    void setParent(EntityHandle child, entt::entity newParent, bool keepWorld);

    void flushDirty(entt::registry& reg, GPUInstanceManager& instanceManager);
    void markDirty(EntityHandle e);

   private:
    static entt::entity to_entity(const EntityHandle& h)
    {
        return h.valid() ? h._entity : entt::null;
    }

    static bool has_transform(const EntityHandle& h)
    {
        return h.valid() && h._reg->any_of<Transform_C>(h._entity);
    }

    static void ensure_chain_up_to_date(EntityHandle e, GPUInstanceManager& instanceManager);
};
}  // namespace batap
