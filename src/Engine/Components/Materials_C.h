#pragma once
#include "Assets/AssetHandle.h"
#include "Reflection/ComponentRegistry.h"
#include <array>
#include <cstdint>

namespace batap
{

struct Materials_C
{
    std::array<MaterialHandle, 8> slots{};
    uint8_t count = 0;
};

// The slot array serializes as 8 path-or-null entries (AssetFieldTypes), so a
// slot keeps its index across a round trip. The inspector keeps its own panel
// for the per-slot asset pickers.
static_assert(refl::fieldName<Materials_C, 0>() == "slots");
static_assert(refl::fieldName<Materials_C, 1>() == "count");

BATAP_COMPONENT(Materials_C, "materials",
                ComponentMeta{.flag = ComponentFlag::Materials, .customEditor = true});

}  // namespace batap
