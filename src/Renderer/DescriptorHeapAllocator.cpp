#include "DescriptorHeapAllocator.h"

#include <stdexcept>

#include "Descriptorhandle.h"

namespace rayvox
{
void DescriptorHeapAllocator::init(ComPtr<ID3D12Device2> device, D3D12_DESCRIPTOR_HEAP_TYPE type_,
                                   UINT numDescriptors)
{
    type = type_;
    capacity = numDescriptors;
    D3D12_DESCRIPTOR_HEAP_DESC descHeapDesc = {};
    descHeapDesc.NumDescriptors = numDescriptors;
    if (type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ||
        type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)
    {
        descHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    }
    else
    {
        descHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    }
    descHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    descHeapDesc.NodeMask = 0;

    HRESULT hr = device->CreateDescriptorHeap(&descHeapDesc, IID_PPV_ARGS(&heap));
    if (FAILED(hr))
    {
        throw std::runtime_error("Failed to create descriptor heap");
    }

    descriptorSize = device->GetDescriptorHandleIncrementSize(type);
    cursor = 0;
}

DescriptorHandle* DescriptorHeapAllocator::alloc()
{
    UINT idx;
    if (!freeList.empty())
    {
        idx = freeList.front();
        freeList.pop();
    }
    else
    {
        if (cursor >= capacity)
        {
            throw std::runtime_error("DescriptorHeapAllocator out of descriptors!");
        }
        idx = cursor++;
    }

    DescriptorHandle* handle = new DescriptorHandle();
    handle->heapIdx = idx;
    handle->cpuHandle.ptr = heap->GetCPUDescriptorHandleForHeapStart().ptr + idx * descriptorSize;
    handle->gpuHandle.ptr = heap->GetGPUDescriptorHandleForHeapStart().ptr + idx * descriptorSize;

    return handle;
}

void DescriptorHeapAllocator::free(const DescriptorHandle& desc)
{
    freeList.push(desc.heapIdx);
    createdDescriptorHandles.erase(desc.heapIdx);
}
void DescriptorHeapAllocator::free(UINT heapIdx)
{
    freeList.push(heapIdx);
    createdDescriptorHandles.erase(heapIdx);
}
}  // namespace rayvox