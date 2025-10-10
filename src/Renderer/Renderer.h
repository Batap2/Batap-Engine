#pragma once
#include <vector>

#include "../Camera.h"
#include "../VoxelDataStructs.h"
#include "CommandQueue.h"
#include "DX12Buffers.h"
#include "DescriptorHeapAllocator.h"
#include "DirectX-Headers/include/directx/d3d12.h"
#include "FenceManager.h"
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

    bool _useVSync = false;
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
    std::vector<CommandQueue> _CommandQueues;

    // Shader layout and pipeline state
    ComPtr<ID3D12RootSignature> _root_signature;
    ComPtr<ID3D12PipelineState> _pso;

    // Resource descriptors(views)
    DescriptorHeapAllocator _descriptorHeapAllocator_CBV_SRV_UAV;
    DescriptorHeapAllocator _descriptorHeapAllocator_SAMPLER;
    DescriptorHeapAllocator _descriptorHeapAllocator_RTV;
    DescriptorHeapAllocator _descriptorHeapAllocator_DSV;

    FenceManager* _fenceManager;

    unsigned int _currentlyInitDescriptor = 0;

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

    void render();

    // Il va falloir scinder les truc du haut qui appartiennent au Renderer et les truc du bas, des
    // objets basique, camera, les voxels etc

    Camera camera;
    DX12ConstantBuffer cameraBuffer;

    std::vector<Voxel> voxelMap;
    DX12StructuredBuffer voxelMapBuffer;

    void InitWorld();
};
}  // namespace rayvox