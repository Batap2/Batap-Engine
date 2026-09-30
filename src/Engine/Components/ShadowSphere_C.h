#pragma once

#include "Reflection/ComponentRegistry.h"

namespace batap
{
// Where no depth map holds the receiver, past the cascades' spheres or out of
// a local light's map, bodies marked with this are shadowed as analytic
// spheres instead.
// Additive: the marker sits on an entity that already has a Mesh_C, see
// ShadowSphereInstance.
struct ShadowSphere_C
{
    // 0 = the mesh's bounding sphere; a value overrides it, for a caster whose
    // silhouette does not follow its geometry.
    float radius_ = 0;
};

BATAP_COMPONENT(ShadowSphere_C, "shadowSphere",
                ComponentMeta{.color = ComponentColor::Orange});
}  // namespace batap
