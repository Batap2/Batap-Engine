#pragma once

#include "Renderer/GPUHandle.h"
#include "Renderer/ResourceManager.h"

namespace rayvox
{
struct Transform_C
{
    GPUHandle buffer_ID;
    byte isDirty = 0;

    // glm::mat4x4 transform;
};
}  // namespace rayvox