#pragma once
#include <vector>
#include <memory>

#include "CommandQueue.h"
#include "DescriptorHeapAllocator.h"
#include "DirectX-Headers/include/directx/d3d12.h"
#include "FenceManager.h"
#include "ResourceManager.h"
#include "RenderGraph.h"

#include "imgui.h"
#include "imgui/backends/imgui_impl_dx12.h"
#include "imgui/backends/imgui_impl_win32.h"
#include "includeDX12.h"

namespace rayvox
{
struct Renderer
{
    uint32_t _threadGroupSizeX = 8;
    uint32_t _threadGroupSizeY = 8;

    UINT _useVSync = 0;
    UINT _tearingFlag = 0;
    bool _fullscreen = false;

    // DXGI
    ComPtr<IDXGIFactory4> _dxgi_factory;
    ComPtr<IDXGISwapChain4> _swapchain;
    static const uint32_t _swapChain_buffer_count = 3;
    uint32_t _buffer_index = 0;

    // D3D12 core interfaces
    ComPtr<ID3D12Debug6> _debug_controller;
    ComPtr<ID3D12Device2> _device;

    // Command interfaces
    std::vector<std::unique_ptr<CommandQueue>> _commandQueues;

    // Shader layout and pipeline state
    ComPtr<ID3D12RootSignature> _root_signature;
    ComPtr<ID3D12PipelineState> _pso;

    // Resource descriptors(views)
    DescriptorHeapAllocator _descriptorHeapAllocator_CBV_SRV_UAV;
    DescriptorHeapAllocator _descriptorHeapAllocator_SAMPLER;
    DescriptorHeapAllocator _descriptorHeapAllocator_RTV;
    DescriptorHeapAllocator _descriptorHeapAllocator_DSV;

    FenceManager* _fenceManager;
    ResourceManager* _resourceManager;
    RenderGraph* _renderGraph;

    bool _isInitialized = false;

    uint32_t _width, _height;

    uint32_t _threadGroupCountX, _threadGroupCountY, _threadGroupCountZ;

    void computeAndUploadCameraBuffer();

    bool setTearingFlag();

    HRESULT compileShaderFromFile(const std::wstring& filename, const std::string& entryPoint,
                                  const std::string& target, ComPtr<ID3DBlob>& shaderBlob);

    void flush();

    void init(HWND hWnd, uint32_t clientWidth, uint32_t clientHeight);
    void initImgui(HWND hwnd, uint32_t clientWidth, uint32_t clientHeight);
    void initRenderPasses();

    void render();
};
}  // namespace rayvox