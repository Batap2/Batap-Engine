#pragma once

#include "IGPUComponent.h"
#include "Renderer/GPU_GUID.h"
#include "Renderer/ResourceManager.h"

#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

namespace rayvox
{
struct TransformGPUData
{
    glm::mat4 modelMatrix;
    glm::mat4 normalMatrix;
};

struct TransformComponent : IGPUComponent
{
    // Données CPU
    glm::vec3 position = {0, 0, 0};
    glm::vec3 rotation = {0, 0, 0};  // Euler angles
    glm::vec3 scale = {1, 1, 1};

    // Handle GPU
    GPU_GUID buffer_ID;
    bool isDirty = true;

    void setPosition(const glm::vec3& pos)
    {
        position = pos;
        isDirty = true;
    }

    void setRotation(const glm::vec3& rot)
    {
        rotation = rot;
        isDirty = true;
    }

    void setScale(const glm::vec3& scl)
    {
        scale = scl;
        isDirty = true;
    }

    TransformGPUData toGPUData() const
    {
        TransformGPUData data;

        // Compute model matrix
        glm::mat4 translation = glm::translate(glm::mat4(1.0f), position);
        glm::mat4 rotX = glm::rotate(glm::mat4(1.0f), rotation.x, glm::vec3(1, 0, 0));
        glm::mat4 rotY = glm::rotate(glm::mat4(1.0f), rotation.y, glm::vec3(0, 1, 0));
        glm::mat4 rotZ = glm::rotate(glm::mat4(1.0f), rotation.z, glm::vec3(0, 0, 1));
        glm::mat4 scaleMatrix = glm::scale(glm::mat4(1.0f), scale);

        

        data.modelMatrix = translation * rotY * rotX * rotZ * scaleMatrix;
        data.normalMatrix = glm::transpose(glm::inverse(data.modelMatrix));

        return data;
    }

    // Interface IGPUComponent
    void allocateGPUResources(ResourceManager* renderer) override
    {
        // buffer_ID = renderer->requestConstantBuffer(sizeof(TransformGPUData));
        // isDirty = true;
    }

    void releaseGPUResources(ResourceManager* renderer) override
    {
        // renderer->releaseBuffer(buffer_ID);
        // buffer_ID = GPU_GUID{};
    }

    void uploadIfDirty(ResourceManager* renderer, uint32_t frameIndex) override
    {
        if (isDirty)
        {
            // TransformGPUData data = toGPUData();
            // renderer->uploadToBuffer(buffer_ID, &data, sizeof(data), frameIndex);
            // isDirty = false;
        }
    }
};
}  // namespace rayvox