#pragma once

#include <entt/entt.hpp>

#include <vector>

#include "Physics/ContactCollector.h"
#include "Physics/ContactEvent.h"

namespace batap
{

struct World;

struct Physics_S
{
    void connectHooks(entt::registry& reg);

    void fixedUpdate(World& world, float dt);

    // After the frame's fixed steps: a dynamic body's transform gets the pose
    // alpha of the way from before the last step to after it, so the render
    // does not jump once per step. A transform read in a fixedUpdate can thus
    // lag Jolt by up to one step.
    void interpolate(World& world, float alpha);

    void drawColliders(World& world);
    bool showColliders_ = false;

    // Contacts of the last step: fixedUpdate fills them after stepping, so the
    // game reads them on the following tick.
    const std::vector<ContactEvent>& contacts() const { return events_; }

   private:
    void onRigidBodyDestroyed(entt::registry& reg, entt::entity e);
    void onRigidBodyChanged(entt::registry& reg, entt::entity e);
    void collectContacts(World& world);

    std::vector<RawContact> rawContacts_;
    std::vector<ContactEvent> events_;
};
}  // namespace batap
