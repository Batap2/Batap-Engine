#pragma once

#include "Renderer/GPU_GUID.h"
#include "Renderer/ResourceManager.h"

#include "glm/ext/matrix_float4x4.hpp"
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

namespace rayvox
{
struct Transform_C
{
    GPU_GUID buffer_ID;
    byte isDirty = 0;

    glm::mat4x4 transform;
};
}  // namespace rayvox