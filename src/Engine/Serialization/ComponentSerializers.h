#pragma once

#include "Components/EntityHandle.h"
#include "EntityDesc.h"

#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string_view>

namespace batap
{

struct Context;
struct World;

struct ComponentHandler
{
    using SerFn = std::optional<nlohmann::json> (*)(EntityHandle, const Context&);
    using DeserFn = void (*)(EntityHandle, const nlohmann::json&, uint32_t version, const Context&,
                             World&);
    using ExtractFn = std::optional<ComponentDesc> (*)(EntityHandle, const Context&);

    std::string_view type;
    uint32_t currentVersion;
    SerFn serialize;
    DeserFn deserialize;
    ExtractFn extract;
};

std::span<const ComponentHandler> getComponentHandlers();

// Returns the handler type string for a given ComponentDesc alternative.
std::string_view componentTypeName(const ComponentDesc& d);

// Desc/Component → JSON helpers
nlohmann::json toJson(const Transform_C& c);
nlohmann::json toJson(const MeshDesc& d);
nlohmann::json toJson(const MaterialsDesc& d);
nlohmann::json toJson(const PointLight_C& c);
nlohmann::json toJson(const Camera_C& c);
nlohmann::json toJson(const SkyboxDesc& d);

}  // namespace batap
