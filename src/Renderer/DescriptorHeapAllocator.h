#pragma once

#include <unordered_map>
#include <wrl.h>
#include <wrl/client.h>
using namespace Microsoft::WRL;
#include <DirectX-Headers/include/directx/d3dx12.h>
#include "Descriptorhandle.h"

#include <queue>

namespace RayVox{
    struct DescriptorHeapAllocator{
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
}