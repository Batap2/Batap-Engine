#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

struct CameraBuffer
{
    glm::vec3 pos;
    float Znear;
    glm::vec3 forward;
    float Zfar;
    glm::vec3 right;
    float fov;
};

struct Camera
{
    glm::vec3 pos{};
    glm::vec3 right{};
    glm::vec3 up{};
    glm::vec3 forward{};

    float fov, aspectRatio, Znear, Zfar, speed = 0.05f;

    Camera() = default;
    Camera(const glm::vec3& position, const glm::vec3& direction, const glm::vec3& upVec,
           float fov, float aspectRatio, float Znear, float Zfar)
            : pos(position), forward(glm::normalize(direction)), up(glm::normalize(upVec)),
              fov(fov), aspectRatio(aspectRatio), Znear(Znear), Zfar(Zfar)
    {
        right = glm::normalize(glm::cross(up, forward));
        up = glm::normalize(glm::cross(forward, right));
    }

    glm::vec3 getPosVec() const
    {
        return pos;
    }

    glm::vec3 getForwardVec() const
    {
        return forward;
    }

    glm::vec3 getUpVec() const
    {
        return up;
    }

    glm::vec3 getRightVec() const
    {
        return right;
    }

    void rotate(const glm::vec3& axis, float angle)
    {
        glm::mat4 rotationMatrix = glm::rotate(glm::mat4(1.0f), angle, axis);

        forward = glm::normalize(glm::mat3(rotationMatrix) * forward);
        right = glm::normalize(glm::mat3(rotationMatrix) * right);
        up = glm::normalize(glm::mat3(rotationMatrix) * up);
    }

    void move(const glm::vec3& direction)
    {
        pos += direction * speed;
    }

    CameraBuffer getCameraBuffer()
    {
        return {pos, Znear, forward, Zfar, right, fov};
    }
};