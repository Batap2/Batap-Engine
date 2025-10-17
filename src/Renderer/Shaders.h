#pragma once

#include <wrl.h>
#include <wrl/client.h>
#include <unordered_map>
using namespace Microsoft::WRL;
#include "DirectX-Headers/include/directx/d3d12.h"

#include <memory>
#include <string>
#include <vector>

namespace rayvox
{
struct Shader
{
    Shader(std::string_view entryPoint, std::string_view target);

    ComPtr<ID3DBlob> _blob;
    std::string _entryPoint;
    std::string _target;

    HRESULT compileShaderFromFile(const std::wstring& filename);
};

struct PipelineState
{
    virtual ~PipelineState() = default;
    virtual void Bind(ID3D12GraphicsCommandList* cmdList) = 0;

    ID3D12PipelineState* _pso = nullptr;
    ID3D12RootSignature* _rootSignature = nullptr;
};

struct GraphicsPipelineState : PipelineState
{
    void Bind(ID3D12GraphicsCommandList* cmdList) override
    {
        cmdList->SetPipelineState(_pso);
        cmdList->SetGraphicsRootSignature(_rootSignature);
    }
};

struct ComputePipelineState : PipelineState
{
    void Bind(ID3D12GraphicsCommandList* cmdList) override
    {
        cmdList->SetPipelineState(_pso);
        cmdList->SetComputeRootSignature(_rootSignature);
    }
};

struct PipelineStateManager
{
    PipelineStateManager() = default;
    ~PipelineStateManager() = default;

    template <typename T>
    T* CreatePipelineState(const std::string& name)
    {
        static_assert(std::is_base_of_v<PipelineState, T>, "T must be derived from PipelineState");
        auto pso = std::make_unique<T>();
        T* raw = pso.get();
        _psos[name] = std::move(pso);
        return raw;
    }

    void BindPipelineState(ID3D12GraphicsCommandList* cmdList, PipelineState* pso);

   private:
    ID3D12RootSignature* _lastBindedRootSignature;
    PipelineState* _lastBindedPSO;
    std::vector<ComPtr<ID3DBlob>> _shaderBlobs;
    std::vector<ComPtr<ID3D12RootSignature>> _rootSignatures;
    std::unordered_map<std::string, std::unique_ptr<PipelineState>> _psos;
};
}  // namespace rayvox