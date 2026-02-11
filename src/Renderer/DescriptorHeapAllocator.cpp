#include "DescriptorHeapAllocator.h"

#include "DebugUtils.h"
#include "Descriptorhandle.h"

#include <stdexcept>

using namespace Microsoft::WRL;

namespace batap
{
void DescriptorHeapAllocator::init(ComPtr<ID3D12Device2> device, D3D12_DESCRIPTOR_HEAP_TYPE type_,
                                   UINT numDescriptors)
{
    type = type_;
    capacity = numDescriptors;
    D3D12_DESCRIPTOR_HEAP_DESC descHeapDesc = {};
    descHeapDesc.Type = type_;
    descHeapDesc.NumDescriptors = numDescriptors;
    if (type_ == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ||
        type_ == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)
    {
        descHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    }
    else
    {
        descHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    }
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
            ThrowRuntime("DescriptorHeapAllocator out of descriptors!");
        }
        idx = cursor++;
    }

    DescriptorHandle* handle = new DescriptorHandle();
    handle->heapIdx = idx;
    handle->cpuHandle.ptr = heap->GetCPUDescriptorHandleForHeapStart().ptr + idx * descriptorSize;
    if (type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
    {
        handle->gpuHandle.ptr =
            heap->GetGPUDescriptorHandleForHeapStart().ptr + idx * descriptorSize;
    }

    return handle;
}

void DescriptorHeapAllocator::free(const DescriptorHandle& desc)
{
    free(desc.heapIdx);
}
void DescriptorHeapAllocator::free(UINT heapIdx)
{
    freeList.push(heapIdx);
    delete createdDescriptorHandles[heapIdx];
    createdDescriptorHandles.erase(heapIdx);
}
}  // namespace batap
