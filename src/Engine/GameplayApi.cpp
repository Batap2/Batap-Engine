#include "Components/EntityHandle.h"
#include "Instance/EntityFactory.h"
#include "Instance/Spawnable.h"
#include "Systems/Hierarchy_S.h"
#include "Systems/Systems.h"
#include "Systems/Transform_S.h"
#include "World.h"

namespace batap
{

static World& worldOf(const EntityHandle& h)
{
    ThrowAssert(h.reg_, "entityHandle has no registry");
    World* const* world = h.reg_->ctx().find<World*>();
    ThrowAssert(world, "registry has no World in its context");
    return **world;
}

static Transform_S& transformsOf(const EntityHandle& h)
{
    return *worldOf(h).systems().transforms_;
}

void EntityHandle::markDirty(ComponentMask changed)
{
    worldOf(*this).instances().markDirty(*this, changed);
}

void EntityHandle::setLocalPosition(const v3f& p)
{
    transformsOf(*this).setLocalPosition(*this, p);
}

void EntityHandle::setLocalRotation(const quatf& q)
{
    transformsOf(*this).setLocalRotation(*this, q);
}

void EntityHandle::setLocalScale(const v3f& s)
{
    transformsOf(*this).setLocalScale(*this, s);
}

void EntityHandle::setPosition(const v3f& p, Space space)
{
    transformsOf(*this).setPosition(*this, p, space);
}

void EntityHandle::setRotation(const quatf& q, Space space)
{
    transformsOf(*this).setRotation(*this, q, space);
}

void EntityHandle::translate(const v3f& vec, Space space)
{
    transformsOf(*this).translate(*this, vec, space);
}

void EntityHandle::rotate(const quatf& delta, Space space)
{
    transformsOf(*this).rotate(*this, delta, space);
}

void EntityHandle::rotate(const v3f& axis, float radians, Space space)
{
    transformsOf(*this).rotate(*this, axis, radians, space);
}

void EntityHandle::scale(const v3f& vec)
{
    transformsOf(*this).scale(*this, vec);
}

void EntityHandle::setParent(EntityHandle newParent)
{
    Hierarchy_S::setParent(*this, newParent);
}

EntityHandle EntityHandle::parent() const
{
    return {reg_, Hierarchy_S::getParent(*this)};
}

EntityHandle World::spawn(std::string_view spawnableId)
{
    return spawn(spawnableFor(spawnableId));
}

EntityHandle World::spawn(const Spawnable& spawnable)
{
    return entityFactory_->create(registry_, spawnable);
}

void World::destroy(EntityHandle h)
{
    entityFactory_->destroy(h);
}
}  // namespace batap
