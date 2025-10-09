#pragma once

#include <wrl.h>
#include <wrl/client.h>

#include <unordered_map>

using namespace Microsoft::WRL;
#include <DirectX-Headers/include/directx/d3dx12.h>

#include <queue>

#include "Descriptorhandle.h"

namespace rayvox
{
struct DescriptorHeapAllocator
{
    ComPtr<ID3D12DescriptorHeap> heap;
    D3D12_DESCRIPTOR_HEAP_TYPE type;
    UINT descriptorSize = 0;
    UINT capacity = 0;
    UINT cursor = 0;
    std::queue<UINT> freeList;

    std::unordered_map<UINT, DescriptorHandle*> createdDescriptorHandles;

    void init(ComPtr<ID3D12Device2> device, D3D12_DESCRIPTOR_HEAP_TYPE type_, UINT numDescriptors);

    DescriptorHandle* alloc();
    void free(const DescriptorHandle& desc);
    void free(UINT heapIdx);
};
}  // namespace rayvox