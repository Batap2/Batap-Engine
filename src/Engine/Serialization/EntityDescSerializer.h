#pragma once

#include "EntityDesc.h"

#include <nlohmann/json.hpp>
#include <string_view>

namespace batap
{

// The importer runs offline: no ECS registry to read components from, and no
// AssetManager holding the assets it has just written, so no handle exists to
// serialize. It emits path-based descs instead, and these turn them into the
// exact json the reflected components read back.
//
// Runtime saving does NOT come through here — it walks the component registry
// (see EntitySerializer::save). Keep the two in sync: a key spelled here must
// match the field name the registry derives, or the importer's output will
// load with default values.

nlohmann::json toJson(const Transform_C& c);
nlohmann::json toJson(const MeshDesc& d);
nlohmann::json toJson(const MaterialsDesc& d);

// The registry type name a given ComponentDesc alternative maps to.
std::string_view componentTypeName(const ComponentDesc& d);

}  // namespace batap
