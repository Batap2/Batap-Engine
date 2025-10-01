#if 0
#include "DX12Context.h"

#include <cstdint>
#include <iostream>
#include <memory>
#include <filesystem>

#include <d3dcompiler.h>
#include "DirectX-Headers/include/directx/d3dx12_resource_helpers.h"

#include "AssertUtils.h"

void DX12Context::EnableDebugLayer()
{
#if defined(_DEBUG)
    // Always enable the debug layer before doing anything DX12 related
    // so all possible errors generated while creating DX12 objects
    // are caught by the debug layer.
    ComPtr<ID3D12Debug> debugInterface;
    if (FAILED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugInterface))))
    {
        throw std::exception();
    }
    debugInterface->EnableDebugLayer();
#endif
}

ComPtr<IDXGIAdapter4> DX12Context::GetAdapter(bool useWarp) {
    ComPtr<IDXGIFactory4> dxgiFactory;
    UINT createFactoryFlags = 0;
#if defined(_DEBUG)
    createFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;
#endif

    if (FAILED(CreateDXGIFactory2(createFactoryFlags, IID_PPV_ARGS(&dxgiFactory)))) {
        throw std::exception();
    }
    ComPtr<IDXGIAdapter1> dxgiAdapter1;
    ComPtr<IDXGIAdapter4> dxgiAdapter4;

    if (useWarp) {
        if (FAILED(dxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&dxgiAdapter1)))) {
            throw std::exception();
        }
        if (FAILED(dxgiAdapter1.As(&dxgiAdapter4))) {
            throw std::exception();
        }
    }
    else
    {
        SIZE_T maxDedicatedVideoMemory = 0;
        for (UINT i = 0; dxgiFactory->EnumAdapters1(i, &dxgiAdapter1) != DXGI_ERROR_NOT_FOUND; ++i)
        {
            DXGI_ADAPTER_DESC1 dxgiAdapterDesc1;
            dxgiAdapter1->GetDesc1(&dxgiAdapterDesc1);

            // Check to see if the adapter can create a D3D12 device without actually
            // creating it. The adapter with the largest dedicated video memory
            // is favored.
            if ((dxgiAdapterDesc1.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0 &&
                SUCCEEDED(D3D12CreateDevice(dxgiAdapter1.Get(),
                                            D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr)) &&
                dxgiAdapterDesc1.DedicatedVideoMemory > maxDedicatedVideoMemory )
            {
                maxDedicatedVideoMemory = dxgiAdapterDesc1.DedicatedVideoMemory;
                if (FAILED(dxgiAdapter1.As(&dxgiAdapter4))) {
                    throw std::exception();
                }
            }
        }
    }

    return dxgiAdapter4;
}

ComPtr<ID3D12Device3> DX12Context::CreateDevice(ComPtr<IDXGIAdapter4>& adapter)
{
    ComPtr<ID3D12Device3> d3d12Device3;
    if (FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&d3d12Device3)))) {
        throw std::exception();
    }

    // Enable debug messages in debug mode.
#if defined(_DEBUG)
    ComPtr<ID3D12InfoQueue> pInfoQueue;
    if (SUCCEEDED(d3d12Device3.As(&pInfoQueue)))
    {
        pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
        pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
        pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, TRUE);
        // Suppress whole categories of messages
        //D3D12_MESSAGE_CATEGORY Categories[] = {};

        // Suppress messages based on their severity level
        D3D12_MESSAGE_SEVERITY Severities[] =
                {
                        D3D12_MESSAGE_SEVERITY_INFO
                };

        // Suppress individual messages by their ID
        D3D12_MESSAGE_ID DenyIds[] = {
                D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,   // I'm really not sure how to avoid this message.
                D3D12_MESSAGE_ID_MAP_INVALID_NULLRANGE,                         // This warning occurs when using capture frame while graphics debugging.
                D3D12_MESSAGE_ID_UNMAP_INVALID_NULLRANGE,                       // This warning occurs when using capture frame while graphics debugging.
        };

        D3D12_INFO_QUEUE_FILTER NewFilter = {};
        // Exemple of ignored warnings
        //NewFilter.DenyList.NumCategories = _countof(Categories);
        //NewFilter.DenyList.pCategoryList = Categories;
        NewFilter.DenyList.NumSeverities = _countof(Severities);
        NewFilter.DenyList.pSeverityList = Severities;
        NewFilter.DenyList.NumIDs = _countof(DenyIds);
        NewFilter.DenyList.pIDList = DenyIds;

        if (FAILED(pInfoQueue->PushStorageFilter(&NewFilter))) {
            throw std::exception();
        }
    }
#endif

    return d3d12Device3;
}

ComPtr<ID3D12CommandQueue> DX12Context::CreateCommandQueue(ComPtr<ID3D12Device2> &device, D3D12_COMMAND_LIST_TYPE type )
{
    ComPtr<ID3D12CommandQueue> d3d12CommandQueue;

    D3D12_COMMAND_QUEUE_DESC desc = {};
    desc.Type =     type;
    desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    desc.Flags =    D3D12_COMMAND_QUEUE_FLAG_NONE;
    desc.NodeMask = 0;

    if (FAILED(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&d3d12CommandQueue)))) {
        throw std::exception();
    }

    return d3d12CommandQueue;
}

bool DX12Context::CheckTearingSupport()
{
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
            if (FAILED(factory5->CheckFeatureSupport(
                    DXGI_FEATURE_PRESENT_ALLOW_TEARING,
                    &allowTearing, sizeof(allowTearing))))
            {
                allowTearing = FALSE;
            }
        }
    }

    return allowTearing == TRUE;
}

ComPtr<IDXGISwapChain4> DX12Context::CreateSwapChain(HWND hWnd,
                                        std::shared_ptr<CommandQueue> &commandQueue,
                                        uint32_t width, uint32_t height, uint32_t bufferCount )
{
    ComPtr<IDXGISwapChain4> dxgiSwapChain4;
    ComPtr<IDXGIFactory4> dxgiFactory4;
    UINT createFactoryFlags = 0;
#if defined(_DEBUG)
    createFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;
#endif

    ThrowIfFailed(CreateDXGIFactory2(createFactoryFlags, IID_PPV_ARGS(&dxgiFactory4)));
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.Width = width;
    swapChainDesc.Height = height;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.Stereo = FALSE;
    swapChainDesc.SampleDesc = { 1, 0 };
    //swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferUsage = DXGI_USAGE_BACK_BUFFER;
    swapChainDesc.BufferCount = bufferCount;
    swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    // It is recommended to always allow tearing if tearing support is available.
    swapChainDesc.Flags = CheckTearingSupport() ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
    ComPtr<IDXGISwapChain1> swapChain1;
    ThrowIfFailed(dxgiFactory4->CreateSwapChainForHwnd(
            commandQueue->GetD3D12CommandQueue().Get(),
            hWnd,
            &swapChainDesc,
            nullptr,
            nullptr,
            &swapChain1));

    // Disable the Alt+Enter fullscreen toggle feature. Switching to fullscreen
    // will be handled manually.
    ThrowIfFailed(dxgiFactory4->MakeWindowAssociation(hWnd, DXGI_MWA_NO_ALT_ENTER));

    ThrowIfFailed(swapChain1.As(&dxgiSwapChain4));

    return dxgiSwapChain4;
}

ComPtr<ID3D12DescriptorHeap> DX12Context::CreateDescriptorHeap(ComPtr<ID3D12Device2> &device,
                                                  D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t numDescriptors,
                                                               D3D12_DESCRIPTOR_HEAP_FLAGS flags)
{
    ComPtr<ID3D12DescriptorHeap> descriptorHeap;

    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.NumDescriptors = numDescriptors;
    desc.Type = type;
    desc.Flags = flags;

    ThrowIfFailed(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&descriptorHeap)));

    return descriptorHeap;
}

void DX12Context::UpdateRenderTargetViews(ComPtr<ID3D12Device2> &device,
                             ComPtr<IDXGISwapChain4> &swapChain, ComPtr<ID3D12DescriptorHeap> &descriptorHeap)
{
    auto rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(descriptorHeap->GetCPUDescriptorHandleForHeapStart());

    for (int i = 0; i < bufferCount; ++i)
    {
        ComPtr<ID3D12Resource> backBuffer;
        ThrowIfFailed(swapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffer)));

        device->CreateRenderTargetView(backBuffer.Get(), nullptr, rtvHandle);

        backBuffers[i] = backBuffer;

        rtvHandle.Offset(rtvDescriptorSize);
    }
}

void DX12Context::UpdateFrameBuffers(ComPtr<ID3D12Device2> &device,
                                     ComPtr<IDXGISwapChain4> &swapChain, ComPtr<ID3D12DescriptorHeap> &descriptorHeap)
{
    auto UAVDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    CD3DX12_CPU_DESCRIPTOR_HANDLE fHandle(descriptorHeap->GetCPUDescriptorHandleForHeapStart());

    for (int i = 0; i < bufferCount; ++i)
    {
        ComPtr<ID3D12Resource> backBuffer;
        ThrowIfFailed(swapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffer)));

        CreateFrameBuffer(backBuffer.GetAddressOf(), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        device->CreateUnorderedAccessView(backBuffer.Get(), nullptr, nullptr, fHandle);

        backBuffers[i] = backBuffer;

        fHandle.Offset(UAVDescriptorSize);
    }
}

ComPtr<ID3D12Fence> DX12Context::CreateFence(ComPtr<ID3D12Device2> &device)
{
    ComPtr<ID3D12Fence> fence;

    ThrowIfFailed(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));

    return fence;
}

HANDLE DX12Context::CreateEventHandle()
{
    HANDLE fenceEvent;

    fenceEvent = ::CreateEvent(NULL, FALSE, FALSE, NULL);
    assert(fenceEvent && "Failed to create fence event.");

    return fenceEvent;
}

void DX12Context::transitionResource(Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> &commandList,
                        Microsoft::WRL::ComPtr<ID3D12Resource> &resource,
                        D3D12_RESOURCE_STATES beforeState, D3D12_RESOURCE_STATES afterState)
{
    CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            resource.Get(),
            beforeState, afterState);

    commandList->ResourceBarrier(1, &barrier);
}

D3D12_CPU_DESCRIPTOR_HANDLE DX12Context::GetCurrentRenderTargetView() const
{
    return CD3DX12_CPU_DESCRIPTOR_HANDLE(RTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
                                         currentBackBufferIndex, RTVDescriptorSize);
}

D3D12_CPU_DESCRIPTOR_HANDLE DX12Context::GetCurrentFrameBufferHandle() const
{
    return CD3DX12_CPU_DESCRIPTOR_HANDLE(RTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
                                         currentBackBufferIndex, RTVDescriptorSize);
}

Microsoft::WRL::ComPtr<ID3D12Resource> DX12Context::GetCurrentBackBuffer() const
{
    return backBuffers[currentBackBufferIndex];
}

void DX12Context::Render()
{
    auto commandList = directCommandQueue->GetCommandList();
    auto backBuffer = GetCurrentBackBuffer();
//    auto rtv = GetCurrentRenderTargetView();
//
//    // Clear the render target.
//    {
//        transitionResource(commandList, backBuffer,
//                           D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
//
//        FLOAT clearColor[] = { 0.4f, 0.6f, 0.9f, 1.0f };
//
//        commandList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
//    }
//
//    commandList->SetPipelineState(basicPipelineState.Get());
//
//    commandList->SetGraphicsRootSignature(basicRootSignature.Get());
//
//    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
//    commandList->IASetVertexBuffers(0, 1, &planeRenderTarget.vertexBufferView);
//    commandList->IASetIndexBuffer(&planeRenderTarget.indexBufferView);
//
//    commandList->RSSetViewports(1, &viewport);
//    commandList->RSSetScissorRects(1, &scissorRect);
//
//    commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
//
//    //auto vec = ReadBackVertexBuffer(device.Get(), commandList.Get(), directCommandQueue->GetD3D12CommandQueue().Get(), planeRenderTarget.vertexBuffer.Get());
//
//    commandList->DrawIndexedInstanced(_countof(planeRenderTarget.indices), 1, 0, 0, 0);
//    // Present
//    {
//        transitionResource(commandList, backBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
//
//        fenceValues[currentBackBufferIndex] = directCommandQueue->ExecuteCommandList(commandList);
//
//        UINT syncInterval = useVSync ? 1 : 0;
//        UINT presentFlags = isTearingSupported && !useVSync ? DXGI_PRESENT_ALLOW_TEARING : 0;
//        ThrowIfFailed(swapChain->Present(syncInterval, presentFlags));
//        currentBackBufferIndex = swapChain->GetCurrentBackBufferIndex();
//
//        directCommandQueue->WaitForFenceValue(fenceValues[currentBackBufferIndex]);
//    }

    commandList->SetPipelineState(computePipelineState.Get());
    commandList->SetComputeRootSignature(computeRootSignature.Get());
    commandList->SetDescriptorHeaps(1, CBV_SRV_UAVDescriptorHeap.GetAddressOf());
    commandList->SetComputeRootDescriptorTable(0, CBV_SRV_UAVDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
    commandList->Dispatch(width/8, height/8, 1);

    retreiveDebugMessage();


    transitionResource(commandList, frameBuffer,
                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    transitionResource(commandList, backBuffer,
                       D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST);

    commandList->CopyResource(backBuffer.Get(), frameBuffer.Get());

    retreiveDebugMessage();

    transitionResource(commandList, frameBuffer,
                       D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    transitionResource(commandList, backBuffer,
                       D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);

    UINT syncInterval = useVSync ? 1 : 0;
    UINT presentFlags = isTearingSupported && !useVSync ? DXGI_PRESENT_ALLOW_TEARING : 0;
    ThrowIfFailed(swapChain->Present(syncInterval, presentFlags));
    currentBackBufferIndex = swapChain->GetCurrentBackBufferIndex();

    directCommandQueue->WaitForFenceValue(fenceValues[currentBackBufferIndex]);

    retreiveDebugMessage();
}

void DX12Context::UpdateBufferResource(ComPtr<ID3D12GraphicsCommandList2> commandList,
                                       ID3D12Resource **pDestinationResource,
                                       ID3D12Resource **pIntermediateResource, size_t numElements, size_t elementSize,
                                       const void *bufferData, D3D12_RESOURCE_FLAGS flags)
{
    size_t bufferSize = numElements * elementSize;

    CD3DX12_HEAP_PROPERTIES heapProperties(D3D12_HEAP_TYPE_DEFAULT);
    CD3DX12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize, flags);

    // Create a committed resource for the GPU resource in a default heap.
    ThrowIfFailed(device->CreateCommittedResource(
            &heapProperties,
            D3D12_HEAP_FLAG_NONE,
           &resourceDesc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(pDestinationResource)));

    // Create an committed resource for the upload.
    if (bufferData)
    {

        CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_UPLOAD);
        CD3DX12_RESOURCE_DESC rd = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);

        ThrowIfFailed(device->CreateCommittedResource(
                &hp,
                D3D12_HEAP_FLAG_NONE,
                &rd,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(pIntermediateResource)));

        D3D12_SUBRESOURCE_DATA subresourceData = {};
        subresourceData.pData = bufferData;
        subresourceData.RowPitch = bufferSize;
        subresourceData.SlicePitch = subresourceData.RowPitch;

        UpdateSubresources(commandList.Get(),
                           *pDestinationResource, *pIntermediateResource,
                           0, 0, 1, &subresourceData);
    }
}

void DX12Context::CreateFrameBuffer(ID3D12Resource **pDestinationResource, D3D12_RESOURCE_FLAGS flags)
{
    CD3DX12_HEAP_PROPERTIES heapProperties(D3D12_HEAP_TYPE_DEFAULT);
    auto resourceDesc = backBuffers[0]->GetDesc();
    resourceDesc.Flags |= flags;
    
    // Create a committed resource for the GPU resource in a default heap.
    ThrowIfFailed(device->CreateCommittedResource(
            &heapProperties,
            D3D12_HEAP_FLAG_NONE,
            &resourceDesc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(pDestinationResource)));
}

HRESULT DX12Context::CompileShaderFromFile(const std::wstring &filename, const std::string &entryPoint,
                                           const std::string &target, ComPtr<ID3DBlob> &shaderBlob) {
    UINT compileFlags = 0;
#ifdef _DEBUG
    compileFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    if (!std::filesystem::exists(filename)) {
        OutputDebugStringA("Shader file not found.\n");
        std::cout << "Shader file not found\n";
        return E_FAIL;
    }

    ComPtr<ID3DBlob> errorBlob;
    HRESULT hr =
            D3DCompileFromFile(
            filename.c_str(),
            nullptr,
            D3D_COMPILE_STANDARD_FILE_INCLUDE,
            entryPoint.c_str(),
            target.c_str(),
            compileFlags,
            0,
            &shaderBlob,
            &errorBlob
    );

    if (FAILED(hr))
    {
        if (errorBlob)
        {
            OutputDebugStringA((char*)errorBlob->GetBufferPointer());
        }
        return hr;
    }

    return S_OK;
}

void DX12Context::Flush() {
    directCommandQueue->Flush();
    computeCommandQueue->Flush();
    copyCommandQueue->Flush();
}

void DX12Context::InitShaders()
{
    auto commandList = copyCommandQueue->GetCommandList();

    // Upload vertex buffer data.
    ComPtr<ID3D12Resource> intermediateVertexBuffer;
    UpdateBufferResource(commandList,
                         &planeRenderTarget.vertexBuffer, &intermediateVertexBuffer,
                         _countof(planeRenderTarget.vertexDataArray), sizeof(VertexData),
                         planeRenderTarget.vertexDataArray);

    CreateFrameBuffer(&frameBuffer, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    device->CreateUnorderedAccessView(frameBuffer.Get(), nullptr, nullptr, CBV_SRV_UAVDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

    // Create the vertex buffer view.
    planeRenderTarget.vertexBufferView.BufferLocation = planeRenderTarget.vertexBuffer->GetGPUVirtualAddress();
    planeRenderTarget.vertexBufferView.SizeInBytes = sizeof(planeRenderTarget.vertexDataArray);
    planeRenderTarget.vertexBufferView.StrideInBytes = sizeof(VertexData);

    // Upload index buffer data.
    ComPtr<ID3D12Resource> intermediateIndexBuffer;
    UpdateBufferResource(commandList,
                         &planeRenderTarget.indexBuffer, &intermediateIndexBuffer,
                         _countof(planeRenderTarget.indices), sizeof(WORD), planeRenderTarget.indices);

    // Create index buffer view.
    planeRenderTarget.indexBufferView.BufferLocation = planeRenderTarget.indexBuffer->GetGPUVirtualAddress();
    planeRenderTarget.indexBufferView.Format = DXGI_FORMAT_R16_UINT;
    planeRenderTarget.indexBufferView.SizeInBytes = sizeof(planeRenderTarget.indices);


    std::wstring shader_dir;
    shader_dir = std::filesystem::current_path().filename() == "bin" ? L"../src/Shaders" : L"src/Shaders";


    // Compilation du Vertex Shader
    ComPtr<ID3DBlob> vertexShaderBlob;
    ThrowIfFailed(CompileShaderFromFile(shader_dir + L"/VertexShader.hlsl", "main", "vs_5_0", vertexShaderBlob));

    // Compilation du Pixel Shader
    ComPtr<ID3DBlob> pixelShaderBlob;
    ThrowIfFailed(CompileShaderFromFile(shader_dir + L"/PixelShader.hlsl", "main", "ps_5_0", pixelShaderBlob));

    ComPtr<ID3DBlob> computeShaderBlob;
    ThrowIfFailed(CompileShaderFromFile(shader_dir + L"/ComputeShader.hlsl", "main", "cs_5_0", computeShaderBlob));

    // Create the vertex input layout
    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            //{ "COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_INPUT_LAYOUT_DESC inputLayoutDesc = {};
    inputLayoutDesc.NumElements = sizeof(inputLayout) / sizeof(D3D12_INPUT_ELEMENT_DESC);
    inputLayoutDesc.pInputElementDescs = inputLayout;

    // Create a root signature.
    D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};
    featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
    if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
    {
        featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
    }

    // Allow input layout and deny unnecessary access to certain pipeline stages.
    D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS;

    // A single 32-bit constant root parameter that is used by the vertex shader.
    //CD3DX12_ROOT_PARAMETER1 rootParameters[1];
    //f(DirectX::XMFLOAT3), 0, 0, D3D12_SHADER_VISIBILITY_VERTEX);

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDescription;
    rootSignatureDescription.Init_1_1(0, nullptr, 0, nullptr, rootSignatureFlags);

    // Serialize the root signature.
    ComPtr<ID3DBlob> basicRootSignatureBlob;
    ComPtr<ID3DBlob> errorBlob;

    ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSignatureDescription,
                                                            featureData.HighestVersion,
                                                            &basicRootSignatureBlob,
                                                            &errorBlob));
    // Create the root signature.
    ThrowIfFailed(device->CreateRootSignature(0, basicRootSignatureBlob->GetBufferPointer(),
                                              basicRootSignatureBlob->GetBufferSize(), IID_PPV_ARGS(&basicRootSignature)));


    {
        D3D12_RT_FORMAT_ARRAY rtvFormats = {};
        rtvFormats.NumRenderTargets = 1;
        rtvFormats.RTFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;

        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.InputLayout = inputLayoutDesc; // the structure describing our input layout
        psoDesc.pRootSignature = basicRootSignature.Get(); // the root signature that describes the input data this pso needs
        psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShaderBlob.Get()); // structure describing where to find the vertex shader bytecode and how large it is
        psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShaderBlob.Get()); // same as VS but for pixel shader
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE; // type of topology we are drawing
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM; // format of the render target
        psoDesc.SampleDesc = { 1, 0 }; // must be the same sample description as the swapchain and depth/stencil buffer
        psoDesc.SampleMask = 0xffffffff; // sample mask has to do with multi-sampling. 0xffffffff means point sampling is done
        psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT); // a default rasterizer state.
        psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT); // a default blent state.
        psoDesc.NumRenderTargets = 1; // we are only binding one render target

        ThrowIfFailed(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&basicPipelineState)));
    }

    // ----------------- Root Signature for Compute Shader ---------------------
    {
        // Description des éléments de la signature racine
        CD3DX12_DESCRIPTOR_RANGE1 ranges[1];
        ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0); // Un UAV à l'emplacement 0

        CD3DX12_ROOT_PARAMETER1 rootParameters[1];
        rootParameters[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_ALL);

        // Définir les flags de la signature racine
        D3D12_ROOT_SIGNATURE_FLAGS computeRootSignatureFlags =
                D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        // Créer la signature racine
        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC computeRootSignatureDesc;
        computeRootSignatureDesc.Init_1_1(ARRAYSIZE(rootParameters), rootParameters, 0, nullptr, computeRootSignatureFlags);

        ComPtr<ID3DBlob> signature;
        ComPtr<ID3DBlob> error;

        ThrowIfFailed(D3D12SerializeVersionedRootSignature(&computeRootSignatureDesc, &signature, &error));

        ThrowIfFailed(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&computeRootSignature)));
    }

    // ------------- PSO for Compute Shader -----------
    {
        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature = computeRootSignature.Get();
        psoDesc.CS.BytecodeLength = computeShaderBlob->GetBufferSize();
        psoDesc.CS.pShaderBytecode = computeShaderBlob->GetBufferPointer();

        ThrowIfFailed(device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&computePipelineState)));
    }


    retreiveDebugMessage();

    auto fenceValue = copyCommandQueue->ExecuteCommandList(commandList);
    copyCommandQueue->WaitForFenceValue(fenceValue);

    isInitialized = true;
}

void DX12Context::InitContext(HWND hWnd, uint32_t clientWidth, uint32_t clientHeight)
{
#if defined(_DEBUG)
    EnableDebugLayer();
    std::cout << "debug version\n";

    ComPtr<ID3D12Debug> debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
        debugController->EnableDebugLayer();
    }

#endif

    width = clientWidth;
    height = clientHeight;

    scissorRect = CD3DX12_RECT(0, 0, LONG_MAX, LONG_MAX);

    isTearingSupported = CheckTearingSupport();

    viewport = CD3DX12_VIEWPORT(0.0f, 0.0f, static_cast<float>(clientWidth), static_cast<float>(clientHeight));

    ComPtr<IDXGIAdapter4> dxgiAdapter4 = GetAdapter(useWarp);

    device = CreateDevice(dxgiAdapter4);

    directCommandQueue = std::make_shared<CommandQueue>(device, D3D12_COMMAND_LIST_TYPE_DIRECT);
    computeCommandQueue = std::make_shared<CommandQueue>(device, D3D12_COMMAND_LIST_TYPE_COMPUTE);
    copyCommandQueue = std::make_shared<CommandQueue>(device, D3D12_COMMAND_LIST_TYPE_COPY);

    swapChain = CreateSwapChain(hWnd, directCommandQueue,
                                clientWidth, clientHeight, bufferCount);

    currentBackBufferIndex = swapChain->GetCurrentBackBufferIndex();

    RTVDescriptorHeap = CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, bufferCount);
    RTVDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    CBV_SRV_UAVDescriptorHeap = CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, bufferCount, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);
    CBV_SRV_UAVDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    UpdateRenderTargetViews(device, swapChain, RTVDescriptorHeap);
    UpdateFrameBuffers(device, swapChain, CBV_SRV_UAVDescriptorHeap);

#if defined(_DEBUG)
    ComPtr<ID3D12InfoQueue> infoQueue;
    if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&infoQueue))))
    {
        // Optional: Set breakpoints on certain severity levels.
        infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
        infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
        infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, TRUE);
    }
#endif
}

void DX12Context::retreiveDebugMessage()
{
    ComPtr<ID3D12InfoQueue> infoQueue;
    if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&infoQueue))))
    {
        UINT64 numMessages = infoQueue->GetNumStoredMessages();
        for (UINT64 i = 0; i < numMessages; i++)
        {
            SIZE_T messageLength = 0;
            infoQueue->GetMessage(i, nullptr, &messageLength); // Get the size of the message

            D3D12_MESSAGE* message = (D3D12_MESSAGE*)malloc(messageLength);
            infoQueue->GetMessage(i, message, &messageLength); // Get the actual message

            OutputDebugStringA(message->pDescription); // Output to the debug console
            std::cout << message->pDescription << "\n";

            free(message); // Free the message memory
        }
    }
}
#endif