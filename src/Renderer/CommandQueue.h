#pragma once

#include "includeDX12.h"
#include <wrl.h>
#include <cstdint>

namespace RayVox {
    struct CommandQueue
    {
        CommandQueue(Microsoft::WRL::ComPtr<ID3D12Device2> &device, D3D12_COMMAND_LIST_TYPE type, uint32_t allocatorNumber);

        struct Command
        {
            uint64_t fenceValue;
            Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator;
            Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList;
        };

        Command& getCommand(uint32_t index);
        uint64_t executeCommand(uint32_t index);
        bool isCommandComplete(Command& cmd) const;
        void waitForFence(uint64_t value);
        void flush();

    
        D3D12_COMMAND_LIST_TYPE                     commandListType;
        Microsoft::WRL::ComPtr<ID3D12Device2>       device;
        Microsoft::WRL::ComPtr<ID3D12CommandQueue>  commandQueue;
        Microsoft::WRL::ComPtr<ID3D12Fence>         fence;
        HANDLE                                      fenceEvent;
        uint64_t                                    fenceValue;
        std::vector<Command> commands;
    };
}