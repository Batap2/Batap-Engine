#include "CommandQueue.h"

#include <handleapi.h>

#include <cstdint>
#include <string>

#include "AssertUtils.h"
#include "DirectX-Headers/include/directx/d3d12.h"
#include "FenceManager.h"

namespace rayvox
{
CommandQueue::CommandQueue(Microsoft::WRL::ComPtr<ID3D12Device2>& device,
                           FenceManager& fenceManager_, D3D12_COMMAND_LIST_TYPE type,
                           uint32_t allocatorNumber)
    : _device(device), _fenceManager(fenceManager_)
{
    D3D12_COMMAND_QUEUE_DESC desc = {};
    desc.Type = type;
    desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    desc.NodeMask = 0;
    ThrowIfFailed(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&_commandQueue)));

    std::string name;
    switch (type)
    {
        case D3D12_COMMAND_LIST_TYPE_DIRECT:
            name = "Fence_CommandQueue_Direct";
            break;
        case D3D12_COMMAND_LIST_TYPE_COMPUTE:
            name = "Fence_CommandQueue_Compute";
            break;
        default:
            name = "Fence_CommandQueue";
    }
    _fenceId = fenceManager_.createFence(name);

    _commands.resize(allocatorNumber);
    for (int i = 0; i < allocatorNumber; ++i)
    {
        auto& cmd = _commands[i];
        std::string nameNum = name + std::to_string(i);
        ThrowIfFailed(device->CreateCommandAllocator(type, IID_PPV_ARGS(&cmd._commandAllocator)));
        std::wstring wname(nameNum.begin(), nameNum.end());
        cmd._commandAllocator->SetName(wname.c_str());
        ThrowIfFailed(device->CreateCommandList(0, type, cmd._commandAllocator.Get(), nullptr,
                                                IID_PPV_ARGS(&cmd._commandList)));
        cmd._commandList->Close();
        cmd._fenceValue = 0;
    }
}

CommandQueue::Command& CommandQueue::getCommand(uint32_t index)
{
    return _commands[index];
}

bool CommandQueue::isCommandComplete(Command& cmd) const
{
    return _fenceManager.isFenceComplete(_fenceId, cmd._fenceValue);
}

uint64_t CommandQueue::executeCommand(uint32_t index)
{
    auto& cmd = _commands[index];
    
    HRESULT hr = cmd._commandList->Close();
    if (FAILED(hr))
    {
        std::cout << "ERROR: CommandList->Close() failed with HRESULT: " << std::hex << hr
                  << std::endl;
        return 0;
    }

    ID3D12CommandList* lists[] = {cmd._commandList.Get()};
    _commandQueue->ExecuteCommandLists(1, lists);

    _fenceValue = _fenceManager.signal(_fenceId, _commandQueue.Get());
    cmd._fenceValue = _fenceValue;

    return _fenceValue;
}

void CommandQueue::waitForFence(Command& cmd) const
{
    _fenceManager.waitForFence(_fenceId, cmd._fenceValue);
}

void CommandQueue::flush()
{
    _fenceManager.flushQueue(_commandQueue.Get());
}
}  // namespace rayvox