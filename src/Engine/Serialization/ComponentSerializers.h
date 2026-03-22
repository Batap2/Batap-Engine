#pragma once

#include "Components/EntityHandle.h"
#include "EntityDesc.h"

#include <nlohmann/json.hpp>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace batap
{

struct Context;
struct World;

struct ComponentHandler
{
    using SerFn   = std::optional<nlohmann::json>(*)(EntityHandle, const Context&);
    using DeserFn = void(*)(EntityHandle, const nlohmann::json&, uint32_t version, const Context&, World&);

    std::string_view type;
    uint32_t         currentVersion;
    SerFn            serialize;
    DeserFn          deserialize;
};

std::span<const ComponentHandler> getComponentHandlers();

// Desc → JSON helpers — single definition of the JSON format per component type,
// used by both the EntityHandle serializers and EntitySerializer::saveTemplate.
nlohmann::json toJson(const TransformDesc& d);
nlohmann::json toJson(const MeshDesc& d);

}  // namespace batap
