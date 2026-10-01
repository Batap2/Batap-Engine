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
    // does not jump once per step.
    void interpolate(World& world, float alpha);

    // Before a fixed step: puts Jolt's own pose back into the transforms
    // interpolate() moved, so a fixedUpdate reads the state the step starts
    // from. Without it gravity is evaluated up to one step behind, which a
    // predictor integrating the real scheme cannot reproduce.
    void restorePoses(World& world);

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
    bool interpolated_ = false;
};
}  // namespace batap
