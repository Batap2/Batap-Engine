#include "Shaders.h"

#include <d3dcompiler.h>
#include <filesystem>
#include <iostream>


namespace rayvox
{

Shader::Shader(std::string_view entryPoint, std::string_view target)
    : _entryPoint(entryPoint), _target(target)
{
}

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

    ComPtr<ID3DBlob> errorBlob;
    HRESULT hr = D3DCompileFromFile(filename.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
                                    _entryPoint.c_str(), _target.c_str(), compileFlags, 0, &_blob,
                                    &errorBlob);

    if (FAILED(hr))
    {
        if (errorBlob)
        {
            OutputDebugStringA((char*) errorBlob->GetBufferPointer());
        }
        return hr;
    }

    return S_OK;
}

void PipelineStateManager::BindPipelineState(ID3D12GraphicsCommandList* cmdList, PipelineState* pso)
{
    if (_lastBindedPSO == pso && _lastBindedRootSignature == pso->_rootSignature)
        return;

    pso->Bind(cmdList);
    _lastBindedPSO = pso;
    _lastBindedRootSignature = pso->_rootSignature;
}

}  // namespace rayvox