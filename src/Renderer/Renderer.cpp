#include "Renderer.h"

#include <directx/d3d12.h>
#include <dxgidebug.h>
#include <wrl/client.h>
#include "Renderer/includeDX12.h"

#include <cstddef>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

#include "CommandQueue.h"
#include "DebugUtils.h"
#include "DescriptorHeapAllocator.h"
#include "FenceManager.h"
#include "RenderGraph.h"
#include "Renderer/EngineConfig.h"
#include "ResourceManager.h"
#include "ResourceName.h"
#include "SceneRenderer.h"
#include "Shaders.h"

#include "imgui.h"
#include "imgui/backends/imgui_impl_dx12.h"
#include "imgui/backends/imgui_impl_win32.h"

#include "tracy/public/tracy/Tracy.hpp"

namespace rayvox
{

struct ImguiUserData
{
    DescriptorHeapAllocator* descriptorHeapAllocator;
    UINT heapIdx = 0;
};
// TODO : leak potentiel
static void imguiSrvAlloc(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* cH,
                          D3D12_GPU_DESCRIPTOR_HANDLE* gH)
{
    ImguiUserData* usrData = static_cast<ImguiUserData*>(info->UserData);

    auto res = usrData->descriptorHeapAllocator->alloc();
    *cH = res->cpuHandle;
    *gH = res->gpuHandle;
    info->UserData = new ImguiUserData{usrData->descriptorHeapAllocator, res->heapIdx};
    delete usrData;
};
static void imguiSrvFree(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE cH,
                         D3D12_GPU_DESCRIPTOR_HANDLE gH)
{
    if (!info->UserData)
        return;

    ImguiUserData* usrData = static_cast<ImguiUserData*>(info->UserData);
    usrData->descriptorHeapAllocator->free(usrData->heapIdx);

    delete usrData;
    info->UserData = nullptr;
};

void Renderer::initImgui(HWND hwnd, uint32_t clientWidth, uint32_t clientHeight)
{
    ImGui_ImplWin32_EnableDpiAwareness();
    float main_scale = ImGui_ImplWin32_GetDpiScaleForMonitor(
        ::MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY));

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();

    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;   // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // Setup scaling
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(
        main_scale);  // Bake a fixed style scale. (until we have a solution for dynamic style
                      // scaling, changing this requires resetting Style + calling this again)
    style.FontScaleDpi =
        main_scale;  // Set initial font scale. (using io.ConfigDpiScaleFonts=true makes this
                     // unnecessary. We leave both here for documentation purpose)

    // Setup Platform/Renderer backends
    ImGui_ImplWin32_Init(hwnd);

    ImGui_ImplDX12_InitInfo init_info = {};
    init_info.Device = _device.Get();
    init_info.CommandQueue = _commandQueues[0]->_commandQueue.Get();
    init_info.NumFramesInFlight = FramesInFlight;
    init_info.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    init_info.DSVFormat = DXGI_FORMAT_UNKNOWN;
    // Allocating SRV descriptors (for textures) is up to the application, so we provide callbacks.
    // (current version of the backend will only allocate one descriptor, future versions will need
    // to allocate more)
    init_info.SrvDescriptorHeap = _resourceManager->_descriptorHeapAllocator_CBV_SRV_UAV.heap.Get();
    init_info.SrvDescriptorAllocFn = imguiSrvAlloc;
    init_info.SrvDescriptorFreeFn = imguiSrvFree;
    init_info.UserData =
        new ImguiUserData{&_resourceManager->_descriptorHeapAllocator_CBV_SRV_UAV, 0};
    ImGui_ImplDX12_Init(&init_info);
}

void Renderer::initRessourcesAndViews(HWND hwnd)
{
    const DXGI_SWAP_CHAIN_DESC1 swapchain_desc = {
        _width,
        _height,
        DXGI_FORMAT_R8G8B8A8_UNORM,
        FALSE,
        {1, 0},
        DXGI_USAGE_BACK_BUFFER,
        FramesInFlight,
        DXGI_SCALING_STRETCH,
        DXGI_SWAP_EFFECT_FLIP_DISCARD,
        DXGI_ALPHA_MODE_UNSPECIFIED,
        _tearingFlag | DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT};

    // Create and upgrade swapchain to our version(ComPtr<IDXGISwapChain4> swapchain)
    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapchain_tier_dx12;
    ThrowIfFailed(_dxgi_factory->CreateSwapChainForHwnd(_commandQueues[0]->_commandQueue.Get(),
                                                        hwnd, &swapchain_desc, nullptr, nullptr,
                                                        &swapchain_tier_dx12));
    ThrowIfFailed(swapchain_tier_dx12.As(&_swapchain));

    ThrowIfFailed(_swapchain->SetMaximumFrameLatency(1));
    _frameLatencyWaitableObject = _swapchain->GetFrameLatencyWaitableObject();

    auto swapChainResourcesHandle =
        _resourceManager->createEmptyFrameResource(toS(RN::texture2D_backbuffers));

    auto swapChainResources = _resourceManager->getFrameResource(swapChainResourcesHandle);
    for (size_t i = 0; i < FramesInFlight; i++)
    {
        _swapchain->GetBuffer(static_cast<UINT>(i),
                              IID_PPV_ARGS(&swapChainResources[i]->_resource));
        swapChainResources[i]->setResource(D3D12_RESOURCE_STATE_PRESENT,
                                           toS(RN::texture2D_backbuffers));
    }

    // compute render resources and views
    {
        auto handle = _resourceManager->createTexture2DFrameResource(
            _width, _height, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_HEAP_TYPE_DEFAULT,
            toS(RN::texture2D_render0));

        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc_render0 = {};
        uavDesc_render0.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        uavDesc_render0.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        uavDesc_render0.Texture2D.MipSlice = 0;
        uavDesc_render0.Texture2D.PlaneSlice = 0;

        _resourceManager->createFrameView<D3D12_UNORDERED_ACCESS_VIEW_DESC>(handle, uavDesc_render0,
                                                                            toS(RN::UAV_render0));
    }

    // imgui resources and views
    {
        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc_imgui = {};
        rtvDesc_imgui.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        rtvDesc_imgui.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        rtvDesc_imgui.Texture2D.MipSlice = 0;
        rtvDesc_imgui.Texture2D.PlaneSlice = 0;

        _resourceManager->createFrameView<D3D12_RENDER_TARGET_VIEW_DESC>(
            swapChainResourcesHandle, rtvDesc_imgui, toS(RN::RTV_imgui));
    }

    // 3D render resources and views
    {
        auto resourceRender3DHandle = _resourceManager->createTexture2DFrameResource(
            _width, _height, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_HEAP_TYPE_DEFAULT,
            toS(RN::texture2D_render3D));

        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc_render3D = {};
        rtvDesc_render3D.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        rtvDesc_render3D.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        rtvDesc_render3D.Texture2D.MipSlice = 0;
        rtvDesc_render3D.Texture2D.PlaneSlice = 0;

        _resourceManager->createFrameView<D3D12_RENDER_TARGET_VIEW_DESC>(
            resourceRender3DHandle, rtvDesc_render3D, toS(RN::RTV_render_3d));

        auto resourceDSVRender3DHandle = _resourceManager->createTexture2DFrameResource(
            _width, _height, DXGI_FORMAT_D32_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL,
            D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_HEAP_TYPE_DEFAULT,
            toS(RN::texture2D_render3D_depthStencil));

        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc_render3D = {};
        dsvDesc_render3D.Format = DXGI_FORMAT_D32_FLOAT;
        dsvDesc_render3D.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        dsvDesc_render3D.Texture2D.MipSlice = 0;

        _resourceManager->createFrameView<D3D12_DEPTH_STENCIL_VIEW_DESC>(
            resourceDSVRender3DHandle, dsvDesc_render3D, toS(RN::DSV_render_3d));
    }
}

void Renderer::initPsosAndShaders()
{
    std::string shader_dir;
    shader_dir = std::filesystem::current_path().filename() == "build" ? "../src/Shaders"
                                                                       : "src/Shaders";
    {
        auto vs = _psoManager->compileShaderFromFile(
            toS(RN::shader_3D_VS), shader_dir + "/VertexShader.hlsl", "main", "vs_5_1");

        auto ps = _psoManager->compileShaderFromFile(
            toS(RN::shader_3D_PS), shader_dir + "/PixelShader.hlsl", "main", "ps_5_1");

        RootSignatureDescription rsDesc_VS{
            {
                DescriptorTableDesc{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0,
                                    D3D12_SHADER_VISIBILITY_ALL},  // Camera
                DescriptorTableDesc{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0,
                                    D3D12_SHADER_VISIBILITY_VERTEX},  // Mesh InstanceData
                RootConstantsDesc{2, 0, 0, D3D12_SHADER_VISIBILITY_ALL}                            // indices : Camera, Mesh
            },
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};

        D3D12_INPUT_ELEMENT_DESC layout[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
             D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 1, 0,
             D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 2, 0,
             D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        };

        _psoManager->createGraphicsPipelineState(
            toS(RN::pso_geometry_pass), rsDesc_VS,
            [&](D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc)
            {
                desc.InputLayout = {layout, _countof(layout)};
                desc.VS = {vs->_blob->GetBufferPointer(), vs->_blob->GetBufferSize()};
                desc.PS = {ps->_blob->GetBufferPointer(), ps->_blob->GetBufferSize()};
                desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
                desc.DepthStencilState.DepthEnable = 1;
                desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
                desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
            });
    }
    {
        auto shader_compute0 = _psoManager->compileShaderFromFile(
            toS(RN::shader_compute0), shader_dir + "/ComputeShader_test.hlsl", "main", "cs_5_1");

        RootSignatureDescription rDesc{{
                                           DescriptorTableDesc{D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1,
                                                               0, 0, D3D12_SHADER_VISIBILITY_ALL},
                                           DescriptorTableDesc{D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1,
                                                               0, 0, D3D12_SHADER_VISIBILITY_ALL},
                                           DescriptorTableDesc{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1,
                                                               0, 0, D3D12_SHADER_VISIBILITY_ALL},
                                       },
                                       D3D12_ROOT_SIGNATURE_FLAG_NONE};

        _psoManager->createComputePipelineState(toS(RN::pso_compute0), rDesc,
                                                [&](D3D12_COMPUTE_PIPELINE_STATE_DESC& d) {
                                                    d.CS = {
                                                        shader_compute0->_blob->GetBufferPointer(),
                                                        shader_compute0->_blob->GetBufferSize()};
                                                });
    }
}

void Renderer::initRenderPasses()
{
    _renderGraph->addPass(toS(RN::pass_composition), D3D12_COMMAND_LIST_TYPE_DIRECT)
        .addRecordStep(
            [this](ID3D12GraphicsCommandList* cmdList, uint32_t frameIndex)
            {
                auto backBuffer =
                    _resourceManager->getFrameResource(RN::texture2D_backbuffers)[_frameIndex];
                auto uav_render0 = _resourceManager->getFrameView(RN::UAV_render0)[_frameIndex];
                auto rtv_render3d = _resourceManager->getFrameView(RN::RTV_render_3d)[_frameIndex];

                backBuffer->transitionTo(cmdList, D3D12_RESOURCE_STATE_COPY_DEST);
                rtv_render3d._resource->transitionTo(cmdList, D3D12_RESOURCE_STATE_COPY_SOURCE);
                cmdList->CopyResource(backBuffer->_resource.Get(), rtv_render3d._resource->get());
                rtv_render3d._resource->transitionTo(cmdList, D3D12_RESOURCE_STATE_RENDER_TARGET);
                backBuffer->transitionTo(cmdList, D3D12_RESOURCE_STATE_PRESENT);
            });

    _renderGraph->addPass(toS(RN::pass_imgui), D3D12_COMMAND_LIST_TYPE_DIRECT)
        .addRecordStep(
            [this](ID3D12GraphicsCommandList* cmdList, uint32_t frameIndex)
            {
                ImGui::Render();
                _ImGuiLastFrameRendered = true;

                auto backBuffer =
                    _resourceManager->getFrameResource(RN::texture2D_backbuffers)[_frameIndex];
                auto rtv_imgui = _resourceManager->getFrameView(RN::RTV_imgui)[_frameIndex];

                ID3D12DescriptorHeap* heaps[] = {
                    _resourceManager->_descriptorHeapAllocator_CBV_SRV_UAV.heap.Get()};
                cmdList->SetDescriptorHeaps(1, heaps);
                backBuffer->transitionTo(cmdList, D3D12_RESOURCE_STATE_RENDER_TARGET);
                cmdList->OMSetRenderTargets(1, &rtv_imgui._descriptorHandle->cpuHandle, FALSE,
                                            nullptr);
                ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmdList);
                backBuffer->transitionTo(cmdList, D3D12_RESOURCE_STATE_PRESENT);
            });
}

bool Renderer::setTearingFlag()
{
    if (_useVSync)
    {
        return false;
    }

    BOOL allowTearing = FALSE;

    // Rather than create the DXGI 1.5 factory interface directly, we create the
    // DXGI 1.4 interface and query for the 1.5 interface. This is to enable the
    // graphics debugging tools which will not support the 1.5 factory interface
    // until a future update.
    Microsoft::WRL::ComPtr<IDXGIFactory4> factory4;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory4))))
    {
        Microsoft::WRL::ComPtr<IDXGIFactory5> factory5;
        if (SUCCEEDED(factory4.As(&factory5)))
        {
            if (FAILED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING,
                                                     &allowTearing, sizeof(allowTearing))))
            {
                allowTearing = FALSE;
            }
        }
    }

    if (allowTearing == TRUE)
    {
        _tearingFlag = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    }
    else
    {
        _useVSync = 1;
    }

    return allowTearing == TRUE;
}

void Renderer::init(HWND hWnd, uint32_t clientWidth, uint32_t clientHeight)
{
    _width = clientWidth;
    _height = clientHeight;

    _threadGroupCountX = (_width + _threadGroupSizeX - 1) / _threadGroupSizeX;
    _threadGroupCountY = (_height + _threadGroupSizeY - 1) / _threadGroupSizeY;
    _threadGroupCountZ = 1;

#ifdef _DEBUG
    /* Get d3d12 debug layer and upgrade it to our version(ComPtr<ID3D12Debug6> debug_controller);
     * And enable validations(has to be done before device creation)
     */
    std::cout << "debug version\n";
    Microsoft::WRL::ComPtr<ID3D12Debug> debug_controller_tier0;
    ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&debug_controller_tier0)));
    ThrowIfFailed(debug_controller_tier0->QueryInterface(IID_PPV_ARGS(&_debug_controller)));
    _debug_controller->SetEnableSynchronizedCommandQueueValidation(true);
    _debug_controller->SetForceLegacyBarrierValidation(true);
    _debug_controller->SetEnableAutoName(true);
    _debug_controller->EnableDebugLayer();
    _debug_controller->SetEnableGPUBasedValidation(true);
#endif

    ThrowIfFailed(CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(&_dxgi_factory)));

    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0;; ++i)
    {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> temp;
        if (_dxgi_factory->EnumAdapters1(i, &temp) == DXGI_ERROR_NOT_FOUND)
            break;

        DXGI_ADAPTER_DESC1 desc;
        temp->GetDesc1(&desc);

        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            continue;    // skip WARP
        adapter = temp;  // premier GPU dédié trouvé
        std::wcout << L"Selected GPU : " << desc.Description << std::endl;
        break;
    }
    // Passing nullptr means its up to system which adapter to use for device, might even use
    // WARP(no gpu)
    ThrowIfFailed(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&_device)));

    _fenceManager = new FenceManager(_device.Get());
    _resourceManager = new ResourceManager(_device.Get(), *_fenceManager, 64 * 1024 * 1024);
    _commandQueues.emplace_back(std::make_unique<CommandQueue>(
        _device, *_fenceManager, D3D12_COMMAND_LIST_TYPE_DIRECT, FramesInFlight));
    _psoManager = new PipelineStateManager(_device.Get());
    _renderGraph = new RenderGraph(_resourceManager);

    setTearingFlag();

    initRessourcesAndViews(hWnd);
    initPsosAndShaders();
    initRenderPasses();
    initImgui(hWnd, clientWidth, clientHeight);

    _isInitialized = true;
}

void Renderer::render()
{
    // Without this, when the window is not focused, the CPU keeps producing frames
    // while the Desktop Window Manager (DWM) consumes them at a much lower rate.
    // This causes the DXGI frame queue to fill up, eventually forcing a long stall
    // (typically observed during Present()).
    WaitForSingleObject(_frameLatencyWaitableObject, INFINITE);

    _renderGraph->execute(_commandQueues, _frameIndex);
    HRESULT hr = _swapchain->Present(_useVSync, _useVSync ? 0 : DXGI_PRESENT_ALLOW_TEARING);

    if (hr == DXGI_STATUS_OCCLUDED)
    {
        std::cout << "_____OCCLUDED\n";
    }

    _psoManager->resetLastBound();
    _frameIndex = (_frameIndex + 1) % FramesInFlight;

    std::chrono::duration<float> frameDuration =
        std::chrono::high_resolution_clock::now() - _lastFrameTimePoint;
    _frameMs = frameDuration.count() * 1000;
    _lastFrameTimePoint = std::chrono::high_resolution_clock::now();
}

void Renderer::beginImGuiFrame()
{
    if (_ImGuiLastFrameRendered)
    {
        ImGui_ImplDX12_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        _ImGuiLastFrameRendered = false;
    }
}

void Renderer::flush()
{
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    for (auto& q : _commandQueues)
    {
        q->flush();
    }

    delete _fenceManager;
    _swapchain.Reset();
    _debug_controller.Reset();
    _device.Reset();

    // ComPtr<IDXGIDebug1> dxgiDebug;
    // DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgiDebug));
    // dxgiDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
}
}  // namespace rayvox
