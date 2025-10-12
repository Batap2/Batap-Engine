#include "Renderer.h"
#include <wrl/client.h>

#include <filesystem>
#include <glm/glm.hpp>
#include <string>

#include "../VoxelDataStructs.h"
#include "AssertUtils.h"
#include "CommandQueue.h"
#include "DescriptorHeapAllocator.h"
#include "DirectX-Headers/include/directx/d3d12.h"
#include "FenceManager.h"
#include "ResourceManager.h"

namespace rayvox
{

struct ImguiUserData
{
    DescriptorHeapAllocator* descriptorHeapAllocator;
    UINT heapIdx = 0;
};
void imguiSrvAlloc(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* cH,
                   D3D12_GPU_DESCRIPTOR_HANDLE* gH)
{
    ImguiUserData* usrData = static_cast<ImguiUserData*>(info->UserData);

    auto res = usrData->descriptorHeapAllocator->alloc();
    *cH = res->cpuHandle;
    *gH = res->gpuHandle;
    info->UserData = new ImguiUserData{usrData->descriptorHeapAllocator, res->heapIdx};
    delete usrData;
};
void imguiSrvFree(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE cH,
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
    (void) io;
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
    init_info.CommandQueue = _CommandQueues[0]._commandQueue.Get();
    init_info.NumFramesInFlight = _swapChain_buffer_count;
    init_info.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    init_info.DSVFormat = DXGI_FORMAT_UNKNOWN;
    // Allocating SRV descriptors (for textures) is up to the application, so we provide callbacks.
    // (current version of the backend will only allocate one descriptor, future versions will need
    // to allocate more)
    init_info.SrvDescriptorHeap = _descriptorHeapAllocator_CBV_SRV_UAV.heap.Get();
    init_info.SrvDescriptorAllocFn = imguiSrvAlloc;
    init_info.SrvDescriptorFreeFn = imguiSrvFree;
    init_info.UserData = new ImguiUserData{&_descriptorHeapAllocator_CBV_SRV_UAV, 0};
    ImGui_ImplDX12_Init(&init_info);
}

void Renderer::InitWorld()
{
    SparseGrid sg;
    sg.DEBUG_fill();
    // voxelMap = sg.getAllVoxels();

    // voxelMapBuffer.CreateOrUpdate(device.Get(), command_list.Get(),
    //	descriptor_heap.Get(), currentlyInitDescriptor, voxelMap);
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
    ComPtr<IDXGIFactory4> factory4;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory4))))
    {
        ComPtr<IDXGIFactory5> factory5;
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
        _tearingFlag = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

    return allowTearing == TRUE;
}

HRESULT Renderer::compileShaderFromFile(const std::wstring& filename, const std::string& entryPoint,
                                        const std::string& target, ComPtr<ID3DBlob>& shaderBlob)
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
                                    entryPoint.c_str(), target.c_str(), compileFlags, 0,
                                    &shaderBlob, &errorBlob);

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

void Renderer::init(HWND hWnd, uint32_t clientWidth, uint32_t clientHeight)
{
    _width = clientHeight;
    _height = clientHeight;

    _threadGroupCountX = (_width + _threadGroupSizeX - 1) / _threadGroupSizeX;
    _threadGroupCountY = (_height + _threadGroupSizeY - 1) / _threadGroupSizeY;
    _threadGroupCountZ = 1;

#ifdef _DEBUG
    /* Get d3d12 debug layer and upgrade it to our version(ComPtr<ID3D12Debug6> debug_controller);
     * And enable validations(has to be done before device creation)
     */
    std::cout << "debug version\n";
    ComPtr<ID3D12Debug> debug_controller_tier0;
    ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&debug_controller_tier0)));
    ThrowIfFailed(debug_controller_tier0->QueryInterface(IID_PPV_ARGS(&_debug_controller)));
    _debug_controller->SetEnableSynchronizedCommandQueueValidation(true);
    _debug_controller->SetForceLegacyBarrierValidation(true);
    _debug_controller->SetEnableAutoName(true);
    _debug_controller->EnableDebugLayer();
    _debug_controller->SetEnableGPUBasedValidation(true);
#endif

    ThrowIfFailed(CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(&_dxgi_factory)));

    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0;; ++i)
    {
        ComPtr<IDXGIAdapter1> temp;
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

    _descriptorHeapAllocator_CBV_SRV_UAV.init(_device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 128);
    _descriptorHeapAllocator_SAMPLER.init(_device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 8);
    _descriptorHeapAllocator_RTV.init(_device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1);
    _descriptorHeapAllocator_DSV.init(_device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1);

    _fenceManager = new FenceManager(_device.Get());
    _resourceManager = new ResourceManager(_device.Get(), *_fenceManager, _swapChain_buffer_count,
                                           64 * 1024 * 1024);
    _CommandQueues.emplace_back(_device, *_fenceManager, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                _swapChain_buffer_count);

    setTearingFlag();

    const DXGI_SWAP_CHAIN_DESC1 swapchain_desc = {_width,
                                                  _height,
                                                  DXGI_FORMAT_R8G8B8A8_UNORM,
                                                  FALSE,
                                                  {1, 0},
                                                  DXGI_USAGE_BACK_BUFFER,
                                                  _swapChain_buffer_count,
                                                  DXGI_SCALING_STRETCH,
                                                  DXGI_SWAP_EFFECT_FLIP_DISCARD,
                                                  DXGI_ALPHA_MODE_UNSPECIFIED,
                                                  _tearingFlag};
    // Create and upgrade swapchain to our version(ComPtr<IDXGISwapChain4> swapchain)
    ComPtr<IDXGISwapChain1> swapchain_tier_dx12;
    ThrowIfFailed(_dxgi_factory->CreateSwapChainForHwnd(_CommandQueues[0]._commandQueue.Get(), hWnd,
                                                        &swapchain_desc, nullptr, nullptr,
                                                        &swapchain_tier_dx12));
    ThrowIfFailed(swapchain_tier_dx12.As(&_swapchain));

    auto swapChainResources = _resourceManager->createEmptyFrameResource(toS(RName::backbuffers));
    for (int i = 0; i < _swapChain_buffer_count; i++)
    {
        _swapchain->GetBuffer(i, IID_PPV_ARGS(&swapChainResources[i]->_resource));
        swapChainResources[i]->setResource(D3D12_RESOURCE_STATE_PRESENT);
    }

    auto texs_render0 = _resourceManager->createTexture2DFrameResource(
        _width, _height, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_HEAP_TYPE_DEFAULT,
        toS(RName::texture2D_render0));

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc_render0 = {};
    uavDesc_render0.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    uavDesc_render0.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uavDesc_render0.Texture2D.MipSlice = 0;
    uavDesc_render0.Texture2D.PlaneSlice = 0;
    _resourceManager->createView(toS(VName::UAV_render0), texs_render0, uavDesc_render0,
                                 _descriptorHeapAllocator_CBV_SRV_UAV);

    camera =
        Camera({0, 0, -10}, {0, 0, 1}, {0, 1, 0}, 80, (float) _width / (float) _height, 0.1f, 100);

    CameraBuffer cameraBufferData = camera.getCameraBuffer();
    // auto camResource = _resourceManager->createBufferResource(
    //     _resourceManager->AlignUp(sizeof(cameraBufferData), 256), D3D12_RESOURCE_STATE_COMMON,
    //     D3D12_HEAP_TYPE_DEFAULT, "camera_buffer");

    //_resourceManager->createCBV("CBV_camera", camResource, _descriptorHeapAllocator_CBV_SRV_UAV);

    std::wstring shader_dir;
    shader_dir = std::filesystem::current_path().filename() == "build" ? L"../src/Shaders"
                                                                       : L"src/Shaders";

    // Shader and its layout
    ComPtr<ID3DBlob> computeShaderBlob;
    ThrowIfFailed(compileShaderFromFile(shader_dir + L"/ComputeShader_test.hlsl", "main", "cs_5_0",
                                        computeShaderBlob));

    {
        // Description des éléments de la signature racine
        CD3DX12_DESCRIPTOR_RANGE1 ranges[3];
        ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1,
                       0);  // Un UAV à l'emplacement 0 | framebuffer
        ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);  // camera
        ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);  // voxel map

        CD3DX12_ROOT_PARAMETER1 rootParameters[3];
        rootParameters[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_ALL);
        rootParameters[1].InitAsDescriptorTable(1, &ranges[1], D3D12_SHADER_VISIBILITY_ALL);
        rootParameters[2].InitAsDescriptorTable(1, &ranges[2], D3D12_SHADER_VISIBILITY_ALL);

        // Définir les flags de la signature racine
        D3D12_ROOT_SIGNATURE_FLAGS computeRootSignatureFlags =
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        // Créer la signature racine
        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC computeRootSignatureDesc;
        computeRootSignatureDesc.Init_1_1(ARRAYSIZE(rootParameters), rootParameters, 0, nullptr,
                                          computeRootSignatureFlags);

        ComPtr<ID3DBlob> signature;
        ComPtr<ID3DBlob> error;

        ThrowIfFailed(
            D3D12SerializeVersionedRootSignature(&computeRootSignatureDesc, &signature, &error));

        ThrowIfFailed(_device->CreateRootSignature(0, signature->GetBufferPointer(),
                                                   signature->GetBufferSize(),
                                                   IID_PPV_ARGS(&_root_signature)));
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc = {};
    pso_desc.pRootSignature = _root_signature.Get();
    pso_desc.CS.BytecodeLength = computeShaderBlob->GetBufferSize();
    pso_desc.CS.pShaderBytecode = computeShaderBlob->GetBufferPointer();
    ThrowIfFailed(_device->CreateComputePipelineState(&pso_desc, IID_PPV_ARGS(&_pso)));

    // initImgui(hWnd, clientWidth, clientHeight);

    _isInitialized = true;

    // InitWorld();
}

inline bool testimgui = true;
void Renderer::render()
{
    std::cout << "=== FRAME ===" << std::endl;
    std::cout << "Buffer index: " << _buffer_index << std::endl;

    auto backBuffer = _resourceManager->getFrameResource(RName::backbuffers)[_buffer_index];
    auto uav_render0 = _resourceManager->getFrameView(VName::UAV_render0)[_buffer_index];

    // ImGui_ImplDX12_NewFrame();
    // ImGui_ImplWin32_NewFrame();
    // ImGui::NewFrame();
    // ImGui::ShowDemoWindow(&testimgui);
    // ImGui::Render();

    for (CommandQueue& queue : _CommandQueues)
    {
        CommandQueue::Command& cmd = queue.getCommand(_buffer_index);

        uint64_t completedValue = _fenceManager->getFence(queue._fenceId)->GetCompletedValue();
        bool complete = queue.isCommandComplete(cmd);
        std::cout << "isCommandComplete: " << complete << " (cmd.fenceValue: " << cmd._fenceValue
                  << ", GPU completed: " << completedValue << ")" << std::endl;

        if (queue.isCommandComplete(cmd))
        {
            std::cout << "RECORDING commands" << std::endl;
            HRESULT hr1 = cmd._commandAllocator->Reset();
            HRESULT hr2 = cmd._commandList->Reset(cmd._commandAllocator.Get(), nullptr);

            if (FAILED(hr1))
                std::cout << "ERROR: CommandAllocator->Reset() failed: " << std::hex << hr1
                          << std::endl;
            if (FAILED(hr2))
                std::cout << "ERROR: CommandList->Reset() failed: " << std::hex << hr2 << std::endl;

            // TODO: Abstraction RenderPass
            auto cmdList = cmd._commandList.Get();

            ID3D12DescriptorHeap* heaps[] = {_descriptorHeapAllocator_CBV_SRV_UAV.heap.Get()};
            cmdList->SetDescriptorHeaps(1, heaps);

            cmdList->SetPipelineState(_pso.Get());
            cmdList->SetComputeRootSignature(_root_signature.Get());

            uav_render0._resource->transitionTo(cmdList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

            cmdList->SetComputeRootDescriptorTable(
                0, uav_render0._descriptorHandle->gpuHandle);  // UAV framebuffer
            // cmdList->SetComputeRootDescriptorTable(1, cameraBufferView.gpuHandle); // CBV camera
            // cmdList->SetComputeRootDescriptorTable(2, voxelMapView.gpuHandle); // SRV voxel map

            cmdList->Dispatch(_threadGroupCountX, _threadGroupCountY, _threadGroupCountZ);

            backBuffer->transitionTo(cmdList, D3D12_RESOURCE_STATE_COPY_DEST);
            uav_render0._resource->transitionTo(cmdList, D3D12_RESOURCE_STATE_COPY_SOURCE);
            cmdList->CopyResource(backBuffer->_resource.Get(), uav_render0._resource->get());
            backBuffer->transitionTo(cmdList, D3D12_RESOURCE_STATE_PRESENT);
            uav_render0._resource->transitionTo(cmdList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

            queue.executeCommand(_buffer_index);
        }
        else
        {
            std::cout << "SKIPPING - GPU still busy!" << std::endl;
        }
    }

    _swapchain->Present(_useVSync, _tearingFlag);
    _buffer_index = (_buffer_index + 1) % _swapChain_buffer_count;
}

void Renderer::flush()
{
    // ImGui_ImplDX12_Shutdown();
    // ImGui_ImplWin32_Shutdown();
    // ImGui::DestroyContext();

    for (auto& q : _CommandQueues)
    {
        q.flush();
    }

    for (int i = 0; i < _swapChain_buffer_count; ++i)
    {
        //_swapchain_buffers[i].resource.Reset();
    }
    _swapchain.Reset();
    _device.Reset();

    delete _fenceManager;
}

void Renderer::computeAndUploadCameraBuffer()
{
    CameraBuffer cameraBufferData = camera.getCameraBuffer();
    // cameraBuffer.CreateOrUpdate(device.Get(), command_list.Get(), cameraBufferData,
    // 	descriptor_heap.Get(), currentlyInitDescriptor);
}
}  // namespace rayvox