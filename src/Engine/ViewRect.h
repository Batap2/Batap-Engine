#pragma once

#include "EigenTypes.h"

namespace batap
{
struct ViewRect
{
    v2i origin_ = v2i::Zero();
    v2i size_ = v2i::Zero();
};
}  // namespace batap
