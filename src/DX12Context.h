#if 0
#pragma once

#include <wrl.h>
using namespace Microsoft::WRL;

// undef minwindef.h macro
#if defined(min)
#undef min
#endif

#if defined(max)
#undef max
#endif

#include <DirectX-Headers/include/directx/d3dx12.h>
#include <dxgi1_6.h>
#include "DX12Utils.h"

#include "CommandQueue.h"
#include "PlaneRenderTarget.h"

struct DX12Context{
    // The number of swap chain back buffers.
    inline static const uint8_t bufferCount = 3;
    // Use WARP adapter
    bool useWarp = false;

    bool isInitialized = false;

    int width, height;
    bool useVSync = false;
    bool isTearingSupported = false;
    bool fullscreen = false;

    // DirectX 12 Objects
    ComPtr<ID3D12Device2> device;
    ComPtr<IDXGISwapChain4> swapChain;
    ComPtr<ID3D12Resource> backBuffers[bufferCount];
    ComPtr<ID3D12Resource> frameBuffer;
    ComPtr<ID3D12DescriptorHeap> RTVDescriptorHeap; // array of rtv for the backbuffers of the swapchain
    UINT RTVDescriptorSize;
    ComPtr<ID3D12DescriptorHeap> CBV_SRV_UAVDescriptorHeap;
    UINT CBV_SRV_UAVDescriptorSize;

    UINT currentBackBufferIndex;
    uint64_t fenceValues[bufferCount] = {};

    std::shared_ptr<CommandQueue> directCommandQueue, computeCommandQueue, copyCommandQueue;

    D3D12_VIEWPORT viewport;
    D3D12_RECT scissorRect;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> basicRootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> basicPipelineState;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> computeRootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> computePipelineState;

    PlaneRenderTarget planeRenderTarget;


    void EnableDebugLayer();

    ComPtr<IDXGIAdapter4> GetAdapter(bool useWarp);

    ComPtr<ID3D12Device3> CreateDevice(ComPtr<IDXGIAdapter4>& adapter);

    ComPtr<ID3D12CommandQueue> CreateCommandQueue(ComPtr<ID3D12Device2> &device, D3D12_COMMAND_LIST_TYPE type);

    bool CheckTearingSupport();

    ComPtr<IDXGISwapChain4> CreateSwapChain(HWND hWnd,
                                            std::shared_ptr<CommandQueue> &commandQueue,
                                            uint32_t width, uint32_t height, uint32_t bufferCount);

    ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(ComPtr<ID3D12Device2> &device,
                                                      D3D12_DESCRIPTOR_HEAP_TYPE type,
                                                      uint32_t numDescriptors,
                                                      D3D12_DESCRIPTOR_HEAP_FLAGS flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE);

    void UpdateRenderTargetViews(ComPtr<ID3D12Device2> &device,
                                 ComPtr<IDXGISwapChain4> &swapChain,
                                 ComPtr<ID3D12DescriptorHeap> &descriptorHeap);

    void UpdateFrameBuffers(ComPtr<ID3D12Device2> &device,
                            ComPtr<IDXGISwapChain4> &swapChain, ComPtr<ID3D12DescriptorHeap> &descriptorHeap);

    ComPtr<ID3D12Fence> CreateFence(ComPtr<ID3D12Device2> &device);

    HANDLE CreateEventHandle();

    void transitionResource(Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> &commandList,
                            Microsoft::WRL::ComPtr<ID3D12Resource> &resource,
                            D3D12_RESOURCE_STATES beforeState, D3D12_RESOURCE_STATES afterState);

    void Render();

    void Flush();

    void InitContext(HWND hWnd, uint32_t clientWidth, uint32_t clientHeight);

    void UpdateBufferResource(ComPtr<ID3D12GraphicsCommandList2> commandList,
                                           ID3D12Resource **pDestinationResource,
                                           ID3D12Resource **pIntermediateResource,
                                           size_t numElements, size_t elementSize,
                                           const void *bufferData, D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE);

    void UpdateBufferResource2D(ID3D12Resource **pDestinationResource, D3D12_RESOURCE_FLAGS flags);

    HRESULT CompileShaderFromFile(const std::wstring& filename, const std::string& entryPoint,
                                  const std::string& target, ComPtr<ID3DBlob>& shaderBlob);

    D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentRenderTargetView() const;

    D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentFrameBufferHandle() const;

    ComPtr<ID3D12Resource> GetCurrentBackBuffer() const;

    void InitShaders();

    void retreiveDebugMessage();

    void CreateFrameBuffer(ID3D12Resource **pDestinationResource, D3D12_RESOURCE_FLAGS flags);
};
#endif