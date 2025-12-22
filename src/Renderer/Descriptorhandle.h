#pragma once

#include <DirectX-Headers/include/directx/d3dx12.h>

namespace rayvox
{
struct DescriptorHandle
{
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle{};
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle{};
    UINT heapIdx = 0;
};
}  // namespace rayvox
