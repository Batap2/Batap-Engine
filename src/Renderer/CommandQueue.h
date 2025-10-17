#pragma once

#include <wrl.h>

#include <cstdint>

#include "includeDX12.h"

namespace rayvox
{

struct FenceManager;

struct CommandQueue
{
    CommandQueue(Microsoft::WRL::ComPtr<ID3D12Device2>& device, FenceManager& fenceManager,
                 D3D12_COMMAND_LIST_TYPE type, uint32_t allocatorNumber);

    struct Command
    {
        uint64_t _fenceValue = 0;
        Microsoft::WRL::ComPtr<ID3D12CommandAllocator> _commandAllocator;
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> _commandList;
    };

    Command& getCommand(uint32_t index);
    uint64_t executeCommand(uint32_t index);
    bool isCommandComplete(Command& cmd) const;
    void waitForFence(Command& cmd) const;
    void flush();

    D3D12_COMMAND_LIST_TYPE _commandListType;
    Microsoft::WRL::ComPtr<ID3D12Device2> _device;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> _commandQueue;
    FenceManager& _fenceManager;
    uint32_t _fenceId;
    uint64_t _fenceValue;

    std::vector<Command> _commands;
};
}  // namespace rayvox