#pragma once

#include "includeDX12.h"
#include "Camera.h"
#include "DX12Buffers.h"

struct DX12ComputeContext
{
    uint32_t threadGroupSizeX = 8;
    uint32_t threadGroupSizeY = 8;

    bool useVSync = true;
    UINT tearingFlag = 0;
    bool fullscreen = false;

    // DXGI
    ComPtr<IDXGIFactory4> dxgi_factory;
    ComPtr<IDXGISwapChain4> swapchain;
    static const uint32_t buffer_count = 3;
    uint32_t buffer_index;

    // D3D12 core interfaces
    ComPtr<ID3D12Debug6> debug_controller;
    ComPtr<ID3D12Device2> device;
    // Command interfaces
    ComPtr<ID3D12CommandQueue> direct_command_queue;
    ComPtr<ID3D12CommandAllocator> command_allocator;
    ComPtr<ID3D12GraphicsCommandList> command_list;

    // Synchronization
    ComPtr<ID3D12Fence> fence;
    uint64_t fence_value;
    HANDLE fence_event;

    // GPU Resources
    ComPtr<ID3D12Resource> swapchain_buffers[buffer_count];
    ComPtr<ID3D12Resource> framebuffer;

    // Shader layout and pipeline state
    ComPtr<ID3D12RootSignature> root_signature;
    ComPtr<ID3D12PipelineState> pso;

    // Resource descriptors(views)
    ComPtr<ID3D12DescriptorHeap> descriptor_heap;

    unsigned int currentlyInitDescriptor = 0;

    bool isInitialized;

    uint32_t width, height;

    uint32_t threadGroupCountX, threadGroupCountY, threadGroupCountZ;

    Camera camera;
    DX12ConstantBuffer cameraBuffer;

    void computeAndUploadCameraBuffer();

    bool setTearingFlag();

    HRESULT CompileShaderFromFile(const std::wstring &filename, const std::string &entryPoint,
                                                      const std::string &target, ComPtr<ID3DBlob> &shaderBlob);

    void flush();

    void init(HWND hWnd, uint32_t clientWidth, uint32_t clientHeight);

    void render();
};