#pragma once
#include <cstddef>

namespace batap
{
constexpr size_t FramesInFlight = 3;
constexpr size_t DescriptorHeapAllocator_CBV_SRV_UAV_size = 1024;
constexpr size_t DescriptorHeapAllocator_RTV_size = 8;
constexpr size_t DescriptorHeapAllocator_DSV_size = 8;
constexpr size_t DescriptorHeapAllocator_Sampler_size = 8;
}  // namespace batap
