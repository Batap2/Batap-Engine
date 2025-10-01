#pragma once

#include <cassert>
#include <winerror.h>
#include <exception>
#include <glm/glm.hpp>
#include <vector>
#include <stdexcept>
#include <wrl.h>
using namespace Microsoft::WRL;
#include <DirectX-Headers/include/directx/d3dx12.h>

inline void ThrowIfFailed(HRESULT hr)
{
    if (FAILED(hr))
    {
        throw std::exception();
    }
}


inline std::vector<glm::vec3> ReadBackVertexBuffer(ID3D12Device* device,
                                                           ID3D12GraphicsCommandList* commandList,
                                                           ID3D12CommandQueue* commandQueue,
                                                           ID3D12Resource* vertexBuffer)
{
    // Describe and create the readback buffer.
    D3D12_RESOURCE_DESC vertexBufferDesc = vertexBuffer->GetDesc();

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_READBACK;

    Microsoft::WRL::ComPtr<ID3D12Resource> readbackBuffer;
    HRESULT hr = device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &vertexBufferDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&readbackBuffer)
    );

    if (FAILED(hr)) {
        throw std::runtime_error("Failed to create readback buffer.");
    }

    // Copy the vertex buffer contents to the readback buffer.
    commandList->CopyResource(readbackBuffer.Get(), vertexBuffer);

    // Execute the command list and wait for it to finish.
    commandList->Close();
    ID3D12CommandList* ppCommandLists[] = { commandList };
    commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

    // Create a fence and an event handle for synchronization.
    Microsoft::WRL::ComPtr<ID3D12Fence> fence;
    UINT64 fenceValue = 1;
    hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    if (FAILED(hr)) {
        throw std::runtime_error("Failed to create fence.");
    }

    HANDLE fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!fenceEvent) {
        throw std::runtime_error("Failed to create fence event.");
    }

    // Signal and wait for the GPU to finish.
    hr = commandQueue->Signal(fence.Get(), fenceValue);
    if (FAILED(hr)) {
        CloseHandle(fenceEvent);
        throw std::runtime_error("Failed to signal command queue.");
    }

    if (fence->GetCompletedValue() < fenceValue) {
        hr = fence->SetEventOnCompletion(fenceValue, fenceEvent);
        if (FAILED(hr)) {
            CloseHandle(fenceEvent);
            throw std::runtime_error("Failed to set event on completion.");
        }
        WaitForSingleObject(fenceEvent, INFINITE);
    }

    CloseHandle(fenceEvent);

    // Map the readback buffer to CPU memory.
    void* pData = nullptr;
    D3D12_RANGE readRange = { 0, 0 }; // We do not intend to write to this resource on the CPU
    hr = readbackBuffer->Map(0, &readRange, &pData);

    if (FAILED(hr)) {
        throw std::runtime_error("Failed to map readback buffer.");
    }

    // Access the data.
    size_t vertexCount = vertexBufferDesc.Width / sizeof(glm::vec3);
    std::vector<glm::vec3> vertices(vertexCount);
    memcpy(vertices.data(), pData, vertexBufferDesc.Width);

    // Unmap the resource.
    D3D12_RANGE writtenRange = { 0, 0 }; // Indicate that we haven't written to the buffer
    readbackBuffer->Unmap(0, &writtenRange);

    return vertices;
}