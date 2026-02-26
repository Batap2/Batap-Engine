#pragma once

#include <cstdint>
#include "Renderer/ResourceFormatWrapper.h"
#include "Renderer/ResourceManager.h"


namespace batap
{
struct Texture
{
    GPUView _GPUTex;
    ResourceFormat _format;
    uint32_t _sizeX;
    uint32_t _sizeY;
};
}  // namespace batap
