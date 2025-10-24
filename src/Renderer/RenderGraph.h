#pragma once

#include <DirectX-Headers/include/directx/d3dx12.h>

#include "CommandQueue.h"
#include "DirectX-Headers/include/directx/d3d12.h"

#include <functional>
#include <memory>
#include <vector>

namespace rayvox
{
struct RenderPass
{
    using RecordFunction = std::function<void(ID3D12GraphicsCommandList*, uint32_t frameIndex)>;

    RenderPass(const std::string& name, D3D12_COMMAND_LIST_TYPE cmdListType = D3D12_COMMAND_LIST_TYPE_DIRECT)
        : _name(name), _commandListType(cmdListType)
    {
    }

    RenderPass& addRecordStep(RecordFunction func)
    {
        _recordSteps.push_back(func);
        return *this;
    }

    void record(ID3D12GraphicsCommandList* cmdList, uint32_t frameIndex)
    {
        for (auto& step : _recordSteps)
        {
            step(cmdList, frameIndex);
        }
    }

    std::string _name;
    D3D12_COMMAND_LIST_TYPE _commandListType;
    std::vector<RecordFunction> _recordSteps;
};

struct RenderGraph
{
    RenderPass& addPass(const std::string& name, D3D12_COMMAND_LIST_TYPE cmdListType)
    {
        _passes.emplace_back(name, cmdListType);
        return _passes.back();
    }

    void execute(std::vector<std::unique_ptr<CommandQueue>>& queues, uint32_t frameIndex)
    {
        for (auto& queue : queues)
        {
            auto& cmd = queue->getCommand(frameIndex);

            if (!queue->isCommandComplete(cmd))
                continue;

            cmd._commandAllocator->Reset();
            cmd._commandList->Reset(cmd._commandAllocator.Get(), nullptr);

            for (auto& pass : _passes)
            {
                if (pass._commandListType != queue->_commandListType)
                    continue;

                for (auto& step : pass._recordSteps)
                    step(cmd._commandList.Get(), frameIndex);
            }

            queue->executeCommand(frameIndex);
        }
    }

    RenderPass* getPass(const std::string& name)
    {
        for (auto& pass : _passes)
        {
            if (pass._name == name)
                return &pass;
        }
        return nullptr;
    }

    void clear()
    {
        _passes.clear();
    }

   private:
    std::vector<RenderPass> _passes;
};
}  // namespace rayvox