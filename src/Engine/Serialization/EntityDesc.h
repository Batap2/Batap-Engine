#pragma once

#include "EigenTypes.h"

#include <string>
#include <variant>
#include <vector>

namespace batap
{

// Intermediate representation of an entity and its components.
// Used by importers (e.g. MeshDecomposer) to pass data to EntitySerializer
// without depending on nlohmann or live ECS state.
// An EntityDesc describes a single entity; a vector<EntityDesc> describes a
// full scene or template, with hierarchy encoded via parentIndex.

struct TransformDesc { v3f pos = v3f::Zero(); quatf rot = quatf::Identity(); v3f scale = v3f::Ones(); };
struct MeshDesc      { std::string path; };

using ComponentDesc = std::variant<TransformDesc, MeshDesc>;

struct EntityDesc
{
    std::string                name;
    int                        parentIndex = -1;  // index into the same vector, -1 = root
    std::vector<ComponentDesc> components;
};

}  // namespace batap
