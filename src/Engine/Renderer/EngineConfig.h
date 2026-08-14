#pragma once
#include <cstddef>
#include <cstdint>

namespace batap
{
constexpr size_t FramesInFlight = 3;
constexpr uint32_t BindlessTextureCapacity = 4096;
constexpr uint64_t StagingBytesPerFrame = 64ull * 1024 * 1024;
}  // namespace batap
