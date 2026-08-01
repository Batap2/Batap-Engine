#pragma once

#include "Components/Transform_C.h"
#include "EigenTypes.h"

#include <array>
#include <string>
#include <variant>
#include <vector>

namespace batap
{

// Intermediate representation of an entity and its components, for importers
// (e.g. MeshDecomposer) that build a scene offline: no ECS registry to read
// components from, and no AssetManager holding the assets they just wrote, so
// no AssetHandle exists to serialize.
//
// Component types are used directly as values when possible:
//   Transform_C — plain data, copied by value.
//
// Exception: MeshDesc / MaterialsDesc carry paths instead of Mesh_C and
// Materials_C, whose handles are indices into a live AssetManager arena.
//
// Saving a running scene does NOT come through here — EntitySerializer::save
// walks the component registry directly. This path exists only because the
// importer has no ECS to walk.

struct MeshDesc
{
    std::string path;
};

struct MaterialsDesc
{
    std::array<std::string, 8> paths{};
    uint8_t count = 0;
};

using ComponentDesc = std::variant<Transform_C, MeshDesc, MaterialsDesc>;

struct EntityDesc
{
    std::string name;
    std::string kind = "empty";  // "mesh" | "pointLight" | "camera" | "skybox" | "empty"
    int parentIndex = -1;        // index into the same vector, -1 = root
    std::vector<ComponentDesc> components;
};

}  // namespace batap
