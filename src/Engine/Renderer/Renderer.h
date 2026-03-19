#pragma once
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "CommandQueue.h"
#include "DescriptorHeapAllocator.h"
#include "FenceManager.h"
#include "Handles.h"
#include "RenderGraph.h"
#include "Renderer/includeDX12.h"
#include "ResourceManager.h"
#include "Shaders.h"


namespace batap
{
struct Renderer
{
    uint32_t _threadGroupSizeX = 8;
    uint32_t _threadGroupSizeY = 8;

    UINT _useVSync = 0;
    UINT _tearingFlag = 0;
    bool _fullscreen = false;

    // DXGI
    Microsoft::WRL::ComPtr<IDXGIFactory4> _dxgi_factory;
    Microsoft::WRL::ComPtr<IDXGISwapChain4> _swapchain;
    HANDLE _frameLatencyWaitableObject;
    uint8_t _frameIndex = 0;
    HWND _hwnd = nullptr;

    // DirectComposition (transparent window)
    Microsoft::WRL::ComPtr<IDCompositionDevice> _dcompDevice;
    Microsoft::WRL::ComPtr<IDCompositionTarget> _dcompTarget;
    Microsoft::WRL::ComPtr<IDCompositionVisual> _dcompVisual;

    // D3D12 core interfaces
    Microsoft::WRL::ComPtr<ID3D12Debug6> _debug_controller;
    Microsoft::WRL::ComPtr<ID3D12Device2> _device;

    // Command interfaces
    std::vector<std::unique_ptr<CommandQueue>> _commandQueues;

    FenceManager* _fenceManager;
    ResourceManager* _resourceManager;
    PipelineStateManager* _psoManager;
    RenderGraph* _renderGraph;

    float _frameMs = 0;
    std::chrono::time_point<std::chrono::high_resolution_clock> _lastFrameTimePoint;

    bool _isInitialized = false;

    bool _ImGuiLastFrameRendered = true;

    uint32_t _width, _height;
    uint32_t _threadGroupCountX, _threadGroupCountY, _threadGroupCountZ;

    void computeAndUploadCameraBuffer();

    bool setTearingFlag();

    HRESULT compileShaderFromFile(const std::wstring& filename, const std::string& entryPoint,
                                  const std::string& target,
                                  Microsoft::WRL::ComPtr<ID3DBlob>& shaderBlob);

    void flush();

    void init(HWND hWnd, uint32_t clientWidth, uint32_t clientHeight);
    void initImgui(HWND hwnd, uint32_t clientWidth, uint32_t clientHeight);
    void initRessourcesAndViews(HWND hwnd);
    void initPsosAndShaders();
    void initRenderPasses();

    void render();
    void beginImGuiFrame();

    // called when window size changes
    void resize(uint32_t w, uint32_t h);

    // Record a callback called after each resize (for systems which have screen size dependant resources)
    using ResizeCallback = std::function<void(uint32_t w, uint32_t h)>;
    void onResize(ResizeCallback cb);

  private:
    void recreateScreenSizeResources();

    // Handles des ressources liées à la taille écran
    GPUResourceHandle swapChainResHandle_;
    GPUResourceHandle render0Handle_;
    GPUResourceHandle render3DHandle_;
    GPUResourceHandle depthHandle_;

    GPUViewHandle uavRender0Handle_;
    GPUViewHandle rtvImguiHandle_;
    GPUViewHandle rtvRender3DHandle_;
    GPUViewHandle dsvHandle_;

    std::vector<ResizeCallback> resizeCallbacks_;
};
}  // namespace batap
