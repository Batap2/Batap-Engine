#pragma once

#include "Instance/InstanceKind.h"

#include <cstdint>

namespace rayvox
{
struct RenderInstance_C
{
    InstanceKind _kind;
    uint32_t _instanceID;
};
}  // namespace rayvox
