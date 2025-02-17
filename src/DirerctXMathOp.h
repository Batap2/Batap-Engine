#pragma once

#include <DirectXMath.h>

namespace DirectXMathOp
{
    using namespace DirectX;

    XMMATRIX CreatePerspectiveProjectionMatrix(float fovY, float aspectRatio, float nearClip, float farClip)
    {
        // Calculer les paramètres de la matrice de projection
        float yScale = 1.0f / tanf(fovY * 0.5f);
        float xScale = yScale / aspectRatio;
        float zRange = farClip - nearClip;

        // Créer la matrice de projection en perspective
        XMMATRIX projectionMatrix = XMMatrixSet(
                xScale, 0.0f, 0.0f, 0.0f,
                0.0f, yScale, 0.0f, 0.0f,
                0.0f, 0.0f, -farClip / zRange, -1.0f,
                0.0f, 0.0f, -nearClip * farClip / zRange, 0.0f
        );
        return projectionMatrix;
    }

    XMMATRIX GetInverseProjectionMatrix(float fovY, float aspectRatio, float nearClip, float farClip)
    {
        XMMATRIX projMatrix = CreatePerspectiveProjectionMatrix(fovY, aspectRatio, nearClip, farClip);
        XMMATRIX invProjMatrix = XMMatrixInverse(nullptr, projMatrix);
        return invProjMatrix;
    }
}