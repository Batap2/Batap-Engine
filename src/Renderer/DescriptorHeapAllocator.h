#pragma once

#include <wrl.h>
#include <wrl/client.h>

#include <unordered_map>

#include "Renderer/includeDX12.h"

#include <queue>

#include "Descriptorhandle.h"

namespace batap
{
struct DescriptorHeapAllocator
{
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap;
    D3D12_DESCRIPTOR_HEAP_TYPE type;
    UINT descriptorSize = 0;
    UINT capacity = 0;
    UINT cursor = 0;
    std::queue<UINT> freeList;

    std::unordered_map<UINT, DescriptorHandle*> createdDescriptorHandles;

    void init(Microsoft::WRL::ComPtr<ID3D12Device2> device, D3D12_DESCRIPTOR_HEAP_TYPE type_,
              UINT numDescriptors);

    DescriptorHandle* alloc();
    void free(const DescriptorHandle& desc);
    void free(UINT heapIdx);
};
}  // namespace batap
