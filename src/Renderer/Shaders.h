#pragma once

#include <wrl/client.h>
#include <string_view>
#include <unordered_map>
#include "DebugUtils.h"

#include "Renderer/includeDX12.h"

#include <functional>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace rayvox
{
struct Shader
{
    Shader(std::string_view entryPoint, std::string_view target);
    ~Shader() = default;

    Microsoft::WRL::ComPtr<ID3DBlob> _blob;
    std::string _entryPoint;
    std::string _target;

    HRESULT compileShaderFromFile(const std::wstring& filename);
};

struct PipelineState
{
    virtual ~PipelineState() = default;
    virtual void bind(ID3D12GraphicsCommandList* cmdList) = 0;

    Microsoft::WRL::ComPtr<ID3D12PipelineState> _pso = nullptr;
    ID3D12RootSignature* _rootSignature = nullptr;
};

struct GraphicsPipelineState : PipelineState
{
    void bind(ID3D12GraphicsCommandList* cmdList) override
    {
        cmdList->SetPipelineState(_pso.Get());
        cmdList->SetGraphicsRootSignature(_rootSignature);
    }
};

struct ComputePipelineState : PipelineState
{
    void bind(ID3D12GraphicsCommandList* cmdList) override
    {
        cmdList->SetPipelineState(_pso.Get());
        cmdList->SetComputeRootSignature(_rootSignature);
    }
};

struct DescriptorTableDesc
{
    D3D12_DESCRIPTOR_RANGE_TYPE type;
    UINT numDescriptors;
    UINT baseShaderRegister;
    UINT registerSpace = 0;

    D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_DESCRIPTOR_RANGE_FLAGS flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
};

// Don't abuse this. Root constants consume 32-bit DWORDs directly in the root signature.
// Wich is limited to 64 DWORDs (shared with root CBV/SRV/UAV).
struct RootConstantsDesc
{
    UINT num32BitValues;  // # of dword
    UINT shaderRegister;  // b#
    UINT registerSpace = 0;

    D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL;
};

using RootParamDesc = std::variant<DescriptorTableDesc, RootConstantsDesc>;

struct RootSignatureDescription
{
    std::vector<RootParamDesc> _params;
    D3D12_ROOT_SIGNATURE_FLAGS _flags;
};

struct PipelineStateManager
{
    PipelineStateManager(ID3D12Device2* device);
    ~PipelineStateManager() = default;

    ID3D12RootSignature* createRootSignature(RootSignatureDescription& desc);

    GraphicsPipelineState*
    createGraphicsPipelineState(const std::string& name,
                                RootSignatureDescription& rootSignatureDesc,
                                std::function<void(D3D12_GRAPHICS_PIPELINE_STATE_DESC&)> configure);

    ComputePipelineState*
    createComputePipelineState(const std::string& name, RootSignatureDescription& rootSignatureDesc,
                               std::function<void(D3D12_COMPUTE_PIPELINE_STATE_DESC&)> configure);

    void bindPipelineState(ID3D12GraphicsCommandList* cmdList, PipelineState* pso);
    void bindPipelineState(ID3D12GraphicsCommandList* cmdList, const std::string& name);

    Shader* compileShaderFromFile(const std::string& name, const std::string& path,
                                  const std::string& entryPoint, const std::string& target);

    PipelineState* getPipelineState(std::string_view name);

    void resetLastBound();

   private:
    ID3D12Device2* _device;
    ID3D12RootSignature* _lastBoundRootSignature = nullptr;
    PipelineState* _lastBoundPSO = nullptr;
    std::vector<Microsoft::WRL::ComPtr<ID3D12RootSignature>> _rootSignatures;
    std::unordered_map<std::string, std::unique_ptr<Shader>> _shaders;
    std::unordered_map<std::string, std::unique_ptr<PipelineState>> _psos;
};
}  // namespace rayvox
