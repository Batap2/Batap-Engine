#include "Shaders.h"

#include "DebugUtils.h"

#include "Renderer/includeDX12.h"

#include <cassert>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

namespace rayvox
{

Shader::Shader(std::string_view entryPoint, std::string_view target)
    : _entryPoint(entryPoint), _target(target)
{}

HRESULT Shader::compileShaderFromFile(const std::wstring& filename)
{
    UINT compileFlags = 0;
#ifdef _DEBUG
    compileFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    if (!std::filesystem::exists(filename))
    {
        OutputDebugStringA("Shader file not found.\n");
        std::cout << "Shader file not found\n";
        return E_FAIL;
    }

    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
    HRESULT hr = D3DCompileFromFile(filename.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
                                    _entryPoint.c_str(), _target.c_str(), compileFlags, 0, &_blob,
                                    &errorBlob);

    if (FAILED(hr))
    {
        if (errorBlob)
        {
            OutputDebugStringA(static_cast<char*>(errorBlob->GetBufferPointer()));
        }
        return hr;
    }

    return S_OK;
}

PipelineStateManager::PipelineStateManager(ID3D12Device2* device) : _device(device) {}

ID3D12RootSignature* PipelineStateManager::createRootSignature(RootSignatureDescription& desc)
{
    std::vector<CD3DX12_DESCRIPTOR_RANGE1> ranges;
    ranges.reserve(desc._descRanges.size());
    std::vector<CD3DX12_ROOT_PARAMETER1> rootParams;
    rootParams.reserve(desc._descRanges.size());

    for (auto& d : desc._descRanges)
    {
        auto& range = ranges.emplace_back();
        range.Init(d.type, d.numDescriptors, d.baseShaderRegister);
        CD3DX12_ROOT_PARAMETER1 param;
        param.InitAsDescriptorTable(1, &range, d.visibility);
        rootParams.push_back(param);
    }

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC vdesc;
    vdesc.Init_1_1(static_cast<UINT>(rootParams.size()), rootParams.data(), 0, nullptr,
                   desc._flags);

    Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
    HRESULT hr = D3D12SerializeVersionedRootSignature(&vdesc, &signatureBlob, &errorBlob);
    if (FAILED(hr))
    {
        if (errorBlob)
        {
            OutputDebugStringA(static_cast<char*>(errorBlob->GetBufferPointer()));
        }
        throw std::runtime_error("Failed to serialize root signature");
    }

    auto& rs = _rootSignatures.emplace_back();

    HRESULT hr2 = _device->CreateRootSignature(0, signatureBlob->GetBufferPointer(),
                                               signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rs));
    if (FAILED(hr2))
    {
        throw std::runtime_error("Failed to create root signature");
    }

    return rs.Get();
}

GraphicsPipelineState* PipelineStateManager::createGraphicsPipelineState(
    const std::string& name, RootSignatureDescription& rootSignatureDesc,
    std::function<void(D3D12_GRAPHICS_PIPELINE_STATE_DESC&)> configure)
{
    ThrowAssert(!_psos.contains(name), "PipelineState already exists");

    auto rootSignature = createRootSignature(rootSignatureDesc);

    auto pso = std::make_unique<GraphicsPipelineState>();
    pso->_rootSignature = rootSignature;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
    desc.pRootSignature = rootSignature;
    desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    desc.SampleMask = UINT_MAX;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;

    if (configure)
        configure(desc);

    HRESULT hr = _device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pso->_pso));
    if (FAILED(hr))
        throw std::runtime_error("Failed to create graphics PSO");

    auto raw = pso.get();
    _psos[name] = std::move(pso);
    return raw;
}

ComputePipelineState* PipelineStateManager::createComputePipelineState(
    const std::string& name, RootSignatureDescription& rootSignatureDesc,
    std::function<void(D3D12_COMPUTE_PIPELINE_STATE_DESC&)> configure)
{
    ThrowAssert(!_psos.contains(name), "PipelineState already exists");

    auto rootSignature = createRootSignature(rootSignatureDesc);

    auto pso = std::make_unique<ComputePipelineState>();
    pso->_rootSignature = rootSignature;

    D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
    desc.pRootSignature = rootSignature;
    desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

    if (configure)
        configure(desc);

    HRESULT hr = _device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&pso->_pso));
    if (FAILED(hr))
        throw std::runtime_error("Failed to create graphics PSO");

    auto raw = pso.get();
    _psos[name] = std::move(pso);
    return raw;
}

void PipelineStateManager::bindPipelineState(ID3D12GraphicsCommandList* cmdList, PipelineState* pso)
{
    if (_lastBoundPSO == pso && _lastBoundRootSignature == pso->_rootSignature)
        return;

    pso->bind(cmdList);
    _lastBoundPSO = pso;
    _lastBoundRootSignature = pso->_rootSignature;
}

void PipelineStateManager::bindPipelineState(ID3D12GraphicsCommandList* cmdList,
                                             const std::string& name)
{
    bindPipelineState(cmdList, _psos[name].get());
}

Shader* PipelineStateManager::compileShaderFromFile(const std::string& name,
                                                    const std::string& path,
                                                    const std::string& entryPoint,
                                                    const std::string& target)
{
    ThrowAssert(!_shaders.contains(name), "shader already exists");
    auto shader = _shaders.emplace(name.data(), std::make_unique<Shader>(entryPoint, target))
                      .first->second.get();
    std::wstring wpath(path.begin(), path.end());
    shader->compileShaderFromFile(wpath);
    return shader;
}

PipelineState* PipelineStateManager::getPipelineState(std::string_view name)
{
    auto it = _psos.find(std::string(name));
    if (it != _psos.end())
        return it->second.get();
    return nullptr;
}

void PipelineStateManager::resetLastBound()
{
    _lastBoundPSO = nullptr;
    _lastBoundRootSignature = nullptr;
}

}  // namespace rayvox
