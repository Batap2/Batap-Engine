#pragma once

#include "Instance/InstanceKind.h"

#include <cstdint>

namespace batap
{
struct RenderInstance_C
{
    InstanceKind _kind;
    uint32_t _instanceID;
};
}  // namespace batap
