#pragma once

#include <cstdint>
#include "Renderer/ResourceManager.h"
#include "Renderer/ResourceFormatWrapper.h"

namespace batap
{
struct Tetxure
{
    GPUView _GPUTex;
    ResourceFormat _format;
    uint32_t _sizeX;
    uint32_t _sizeY;
};
}  // namespace batap
