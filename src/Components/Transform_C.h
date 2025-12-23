#pragma once

#include "Handles.h"
#include "Renderer/ResourceManager.h"

namespace rayvox
{
struct Transform_C
{
    GPUViewHandle buffer_ID;
    byte isDirty = 0;

    // glm::mat4x4 transform;
};
}  // namespace rayvox
