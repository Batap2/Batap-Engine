#pragma once

#include <DirectX-Headers/include/directx/d3dx12.h>

#include "CommandQueue.h"

#include <functional>
#include <memory>
#include <vector>

namespace rayvox
{
struct RenderPass
{
    enum class QueueType
    {
        Direct,
        Compute,
        Copy
    };

    using RecordFunction = std::function<void(ID3D12GraphicsCommandList*, uint32_t frameIndex)>;

    RenderPass(const std::string& name, QueueType queue = QueueType::Direct)
        : _name(name), _queueType(queue)
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
    QueueType _queueType;
    std::vector<RecordFunction> _recordSteps;
};

struct RenderGraph
{
    RenderPass& addPass(const std::string& name, RenderPass::QueueType queueType)
    {
        _passes.emplace_back(name, queueType);
        return _passes.back();
    }

    void execute(std::vector<std::unique_ptr<CommandQueue>>& queues, uint32_t frameIndex)
    {
        for (auto& pass : _passes)
        {
            auto* queue = queues[static_cast<int>(pass._queueType)].get();
            auto& cmd = queue->getCommand(frameIndex);

            if (!queue->isCommandComplete(cmd))
            {
                continue;
            }

            cmd._commandAllocator->Reset();
            cmd._commandList->Reset(cmd._commandAllocator.Get(), nullptr);
            for (auto& step : pass._recordSteps)
                step(cmd._commandList.Get(), frameIndex);

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