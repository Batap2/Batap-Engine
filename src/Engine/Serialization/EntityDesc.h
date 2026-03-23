#pragma once

#include "Components/Camera_C.h"
#include "Components/PointLight_C.h"
#include "Components/Transform_C.h"
#include "EigenTypes.h"

#include <string>
#include <variant>
#include <vector>

namespace batap
{

// Intermediate representation of an entity and its components.
// Used by importers (e.g. MeshDecomposer) and EntitySerializer::save() to pass
// data to serializeEntityDescs() without depending on nlohmann or live ECS state.
//
// Component types are used directly as values (no ECS) when possible:
//   Transform_C, PointLight_C, Camera_C — plain data, copied by value.
//
// Exception: MeshDesc (path string) instead of Mesh_C (AssetHandle<Mesh>).
// Mesh_C is an opaque handle to a GPU-loaded resource; its serializable
// representation is a path that lives in the AssetManager — not the handle.

struct MeshDesc { std::string path; };

using ComponentDesc = std::variant<Transform_C, MeshDesc, PointLight_C, Camera_C>;

struct EntityDesc
{
    std::string                name;
    std::string                kind        = "empty"; // "mesh" | "pointLight" | "camera" | "empty"
    int                        parentIndex = -1;      // index into the same vector, -1 = root
    std::vector<ComponentDesc> components;
};

}  // namespace batap
