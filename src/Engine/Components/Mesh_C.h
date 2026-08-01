#pragma once

#include "Assets/AssetHandle.h"
#include "Reflection/ComponentRegistry.h"

namespace batap
{
struct Mesh_C
{
    MeshHandle _mesh;
};

// The handle is serialized as its asset path (AssetFieldTypes); the inspector
// keeps its own panel for the asset picker, which the generic field loop
// cannot express.
static_assert(refl::fieldName<Mesh_C, 0>() == "mesh");

BATAP_COMPONENT(Mesh_C, "mesh",
                ComponentMeta{.flag = ComponentFlag::Mesh, .customEditor = true});
}  // namespace batap
