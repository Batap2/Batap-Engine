#pragma once
#include <cstdint>

namespace batap
{
constexpr uint32_t FramesInFlight = 3;
constexpr uint32_t BindlessTextureCapacity = 4096;
constexpr uint64_t StagingBytesPerFrame = 64ull * 1024 * 1024;
constexpr uint32_t LocalAtlasSize = 4096;
constexpr uint32_t LocalTileMin = 128;
constexpr uint32_t LocalTileMax = 1024;
// The light's shadow is gone at this resolution, full at LocalTileMin.
constexpr uint32_t LocalTileFade = 64;
constexpr float LocalClassHysteresis = 1.2f;
constexpr uint32_t CascadeAtlasSize = 4096;
constexpr uint32_t CascadeTileSize = 2048;
// Range the cascades are fitted over. Only the fit reads it: the partition
// with the analytic occluders follows the spheres the fit made (R8).
constexpr float CascadeRange = 200.f;
}  // namespace batap
