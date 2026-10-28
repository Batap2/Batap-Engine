#pragma once
#include <vector>
#include "Assets/AssetHandle.h"
#include "Reflection/ComponentRegistry.h"

namespace batap
{

struct Materials_C
{
    std::vector<MaterialHandle> slots;
};

// One slot per submesh, indexed by submesh: a null slot falls back to the
// default material, so how many the mesh actually uses is the mesh's business.
static_assert(refl::fieldName<Materials_C, 0>() == "slots");

BATAP_COMPONENT(Materials_C, "materials",
                ComponentMeta{.color = ComponentColor::Yellow, .customEditor = true});

}  // namespace batap
