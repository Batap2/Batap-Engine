#include "CommandQueue.h"
#include "AssertUtils.h"
#include "DirectX-Headers/include/directx/d3d12.h"
#include <cstdint>
#include <handleapi.h>

namespace RayVox{
    CommandQueue::CommandQueue(Microsoft::WRL::ComPtr<ID3D12Device2> &device, D3D12_COMMAND_LIST_TYPE type, uint32_t allocatorNumber){
        D3D12_COMMAND_QUEUE_DESC desc = {};
        desc.Type = type;
        desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
        desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        desc.NodeMask = 0;
        ThrowIfFailed(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&commandQueue)));

        ThrowIfFailed(device->CreateFence(fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));

        fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        assert(fenceEvent && "Failed to create fence event");

        commands.resize(allocatorNumber);
        for (auto& cmd : commands)
        {
            ThrowIfFailed(device->CreateCommandAllocator(type, IID_PPV_ARGS(&cmd.commandAllocator)));
            ThrowIfFailed(device->CreateCommandList(
                0,
                type,
                cmd.commandAllocator.Get(),
                nullptr,
                IID_PPV_ARGS(&cmd.commandList)
            ));
            cmd.commandList->Close();
            cmd.fenceValue = 0;
        }
    }

    CommandQueue::Command& CommandQueue::getCommand(uint32_t index){
        return commands[index];
    }

    bool CommandQueue::isCommandComplete(Command& cmd) const
    {
        return fence->GetCompletedValue() >= cmd.fenceValue;
    }

    uint64_t CommandQueue::executeCommand(uint32_t index){
        auto& cmd =  commands[index];

        cmd.commandList->Close();
        ID3D12CommandList* lists[] = {cmd.commandList.Get()};
        commandQueue->ExecuteCommandLists(1, lists);
        fenceValue++;
        commandQueue->Signal(fence.Get(), fenceValue);
        cmd.fenceValue = fenceValue;
        
        return fenceValue;
    }

    void CommandQueue::flush(){
        fenceValue++;
        commandQueue->Signal(fence.Get(), fenceValue);

        if(fence->GetCompletedValue() < fenceValue) {
            fence->SetEventOnCompletion(fenceValue, fenceEvent);
            WaitForSingleObject(fenceEvent, INFINITE);
        }
        CloseHandle(fenceEvent);
    }
}