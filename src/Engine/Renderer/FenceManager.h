#pragma once

#include <wrl/client.h>

#include "Renderer/includeDX12.h"

namespace batap
{

struct FenceManager
{
   public:
    FenceManager(ID3D12Device* device) : _device(device)
    {
        _fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    }

    ~FenceManager() { CloseHandle(_fenceEvent); }

    uint32_t createFence(const std::string& name = "")
    {
        uint32_t id = static_cast<uint32_t>(_fences.size());

        FenceData data;
        _device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&data._fence));
        data._currentValue = 0;
        data.name = name;

        _fences.push_back(std::move(data));
        return id;
    }

    uint64_t signal(uint32_t fenceId, ID3D12CommandQueue* commandQueue)
    {
        auto& data = _fences[fenceId];
        data._currentValue++;
        commandQueue->Signal(data._fence.Get(), data._currentValue);
        return data._currentValue;
    }

    void waitForFence(uint32_t fenceId, uint64_t value)
    {
        auto& data = _fences[fenceId];

        if (data._fence->GetCompletedValue() < value)
        {
            data._fence->SetEventOnCompletion(value, _fenceEvent);
            WaitForSingleObject(_fenceEvent, INFINITE);
        }
    }

    void waitForLastSignal(uint32_t fenceId)
    {
        auto& data = _fences[fenceId];
        waitForFence(fenceId, data._currentValue);
    }

    bool isFenceComplete(uint32_t fenceId, uint64_t value)
    {
        return _fences[fenceId]._fence->GetCompletedValue() >= value;
    }

    void flushQueue(ID3D12CommandQueue* commandQueue)
    {
        uint32_t tempFenceId = createFence("FlushFence");
        signal(tempFenceId, commandQueue);
        waitForLastSignal(tempFenceId);
    }

    ID3D12Fence* getFence(uint32_t fenceId) { return _fences[fenceId]._fence.Get(); }

    uint64_t getCurrentValue(uint32_t fenceId) { return _fences[fenceId]._currentValue; }

   private:
    struct FenceData
    {
        Microsoft::WRL::ComPtr<ID3D12Fence> _fence;
        uint64_t _currentValue = 0;
        std::string name;
    };

    ID3D12Device* _device;
    std::vector<FenceData> _fences;
    HANDLE _fenceEvent;
};

}  // namespace batap
