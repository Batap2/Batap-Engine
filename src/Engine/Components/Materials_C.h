#pragma once
#include "Assets/AssetHandle.h"
#include "Reflection/ComponentRegistry.h"
#include <array>

namespace batap
{

struct Materials_C
{
    std::array<MaterialHandle, 8> slots{};
};

// One slot per submesh, indexed by submesh: a null slot falls back to the
// default material, so how many the mesh actually uses is the mesh's business.
// The array serializes as 8 path-or-null entries (AssetFieldTypes), so a slot
// keeps its index across a round trip. The inspector keeps its own panel for
// the per-slot asset pickers.
static_assert(refl::fieldName<Materials_C, 0>() == "slots");

BATAP_COMPONENT(Materials_C, "materials", ComponentMeta{.customEditor = true});

}  // namespace batap
