#pragma once

#include "Renderer/GPU_GUID.h"
#include "Renderer/ResourceManager.h"

#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

namespace rayvox
{
struct Camera_C
{
    GPU_GUID buffer_ID;
    byte isDirty = 0;

    glm::vec3 pos;
    float Znear;
    glm::vec3 forward;
    float Zfar;
    glm::vec3 right;
    float fov;
};
}  // namespace rayvox