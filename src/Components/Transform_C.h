#pragma once

#include "Renderer/GPU_GUID.h"
#include "Renderer/ResourceManager.h"


namespace rayvox
{
struct Transform_C
{
    GPU_GUID buffer_ID;
    byte isDirty = 0;

    //glm::mat4x4 transform;
};
}  // namespace rayvox