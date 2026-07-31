#pragma once

#include "Components/Camera_C.h"
#include "Components/Transform_C.h"
#include "EigenTypes.h"

#include <array>
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
//   Transform_C, Camera_C — plain data, copied by value.
//
// Exception: MeshDesc (path string) instead of Mesh_C (AssetHandle<Mesh>).
// Mesh_C is an opaque handle to a GPU-loaded resource; its serializable
// representation is a path that lives in the AssetManager — not the handle.

struct MeshDesc
{
    std::string path;
};

struct MaterialsDesc
{
    std::array<std::string, 8> paths{};
    uint8_t count = 0;
};

struct SkyboxDesc
{
    std::string hdriPath;
    uint32_t mode = 0;
    Eigen::Vector3f color1 = {0.5f, 0.72f, 0.90f};   // ciel
    Eigen::Vector3f color2 = {0.80f, 0.88f, 1.00f};  // horizon
    Eigen::Vector3f color3 = {0.25f, 0.20f, 0.15f};  // bas
    float horizonWidth = 0.15f;
    float intensity    = 1.0f;
};

using ComponentDesc =
    std::variant<Transform_C, MeshDesc, MaterialsDesc, Camera_C, SkyboxDesc>;

struct EntityDesc
{
    std::string name;
    std::string kind = "empty";  // "mesh" | "pointLight" | "camera" | "skybox" | "empty"
    int parentIndex = -1;        // index into the same vector, -1 = root
    std::vector<ComponentDesc> components;
};

}  // namespace batap
