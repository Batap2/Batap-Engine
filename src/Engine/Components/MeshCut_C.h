#pragma once

#include "EigenTypes.h"
#include "Reflection/ComponentRegistry.h"

namespace batap
{
struct MeshCut_C
{
    v3f point_ = v3f::Zero();
    v3f up_ = v3f::UnitY();
    bool keepAbove_ = false;
};

BATAP_COMPONENT(MeshCut_C, "meshCut", ComponentMeta{.color = ComponentColor::Violet});
}  // namespace batap
