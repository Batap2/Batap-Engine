#include "DX12ComputeContext.h"
#include "AssertUtils.h"

#include <DirectXMath.h>
#include <filesystem>
#include <optional>

void DX12ComputeContext::InitWorld()
{
	for (int x = 0; x < 32; x++)
	{
		for (int y = 0; y < 32; y++)
		{
			VoxelDataStructs::Voxel v;
			v.pos = XMUINT3(x, y, 0);
			voxelMap.push_back(v);
		}
	}

	voxelMapBuffer.CreateOrUpdate(device.Get(), command_list.Get(),
		descriptor_heap.Get(), currentlyInitDescriptor, voxelMap);


}

bool DX12ComputeContext::setTearingFlag()
{
	if (useVSync)
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
			if (FAILED(factory5->CheckFeatureSupport(
				DXGI_FEATURE_PRESENT_ALLOW_TEARING,
				&allowTearing, sizeof(allowTearing))))
			{
				allowTearing = FALSE;
			}
		}
	}

	if (allowTearing == TRUE)
		tearingFlag = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

	return allowTearing == TRUE;
}

HRESULT DX12ComputeContext::CompileShaderFromFile(const std::wstring& filename, const std::string& entryPoint,
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
			&errorBlob);

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

void DX12ComputeContext::init(HWND hWnd, uint32_t clientWidth, uint32_t clientHeight)
{
	width = clientHeight;
	height = clientHeight;

	threadGroupCountX = (width + threadGroupSizeX - 1) / threadGroupSizeX;
	threadGroupCountY = (height + threadGroupSizeY - 1) / threadGroupSizeY;
	threadGroupCountZ = 1;

#ifdef _DEBUG
	/* Get d3d12 debug layer and upgrade it to our version(ComPtr<ID3D12Debug6> debug_controller);
	 * And enable validations(has to be done before device creation)
	 */
	std::cout << "debug version\n";
	ComPtr<ID3D12Debug> debug_controller_tier0;
	ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&debug_controller_tier0)));
	ThrowIfFailed(debug_controller_tier0->QueryInterface(IID_PPV_ARGS(&debug_controller)));
	debug_controller->SetEnableSynchronizedCommandQueueValidation(true);
	debug_controller->SetForceLegacyBarrierValidation(true);
	debug_controller->SetEnableAutoName(true);
	debug_controller->EnableDebugLayer();
	debug_controller->SetEnableGPUBasedValidation(true);
#endif

	// Passing nullptr means its up to system which adapter to use for device, might even use WARP(no gpu)
	ThrowIfFailed(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));

	// TODO trouver un moyen de gérer ce foutu nombre de descriptor dans le heap
	const D3D12_DESCRIPTOR_HEAP_DESC descriptor_heap_desc{
		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
		3,
		D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE };
	ThrowIfFailed(device->CreateDescriptorHeap(&descriptor_heap_desc, IID_PPV_ARGS(&descriptor_heap)));

	const D3D12_COMMAND_QUEUE_DESC command_queue_desc = {
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		D3D12_COMMAND_QUEUE_PRIORITY_NORMAL,
		D3D12_COMMAND_QUEUE_FLAG_NONE,
		0 };
	ThrowIfFailed(device->CreateCommandQueue(&command_queue_desc, IID_PPV_ARGS(&direct_command_queue)));

	ThrowIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
		IID_PPV_ARGS(&command_allocator)));

	ThrowIfFailed(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
		command_allocator.Get(), nullptr, IID_PPV_ARGS(&command_list)));

	ThrowIfFailed(command_list->Close());
	ThrowIfFailed(command_allocator->Reset());
	ThrowIfFailed(command_list->Reset(command_allocator.Get(), nullptr));

	fence_value = 0;
	ThrowIfFailed(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
	fence_event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
	if (!fence_event)
	{
		ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
	}

	setTearingFlag();

	// Factory to create swapchains etc
	ThrowIfFailed(CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(&dxgi_factory)));

	const DXGI_SWAP_CHAIN_DESC1 swapchain_desc = {
		width,
		height,
		DXGI_FORMAT_R8G8B8A8_UNORM,
		FALSE,
		{1, 0},
		DXGI_USAGE_BACK_BUFFER,
		buffer_count,
		DXGI_SCALING_STRETCH,
		DXGI_SWAP_EFFECT_FLIP_DISCARD,
		DXGI_ALPHA_MODE_UNSPECIFIED,
		tearingFlag };
	// Create and upgrade swapchain to our version(ComPtr<IDXGISwapChain4> swapchain)
	ComPtr<IDXGISwapChain1> swapchain_tier_dx12;
	ThrowIfFailed(dxgi_factory->CreateSwapChainForHwnd(
		direct_command_queue.Get(),
		hWnd,
		&swapchain_desc,
		nullptr,
		nullptr,
		&swapchain_tier_dx12));
	ThrowIfFailed(swapchain_tier_dx12.As(&swapchain));

	// Get swapchain pointers to ID3D12Resource's that represents buffers
	for (int i = 0; i < buffer_count; i++)
	{
		ThrowIfFailed(swapchain->GetBuffer(i, IID_PPV_ARGS(&swapchain_buffers[i].resource)));
		ThrowIfFailed(swapchain_buffers[i].resource->SetName(L"swapchain_buffer"));
	}

	// Retrieve swapchain buffer description and create identical resource but with UAV allowed, so compute shader could write to it
	auto buffer_desc = swapchain_buffers[0].resource->GetDesc();
	buffer_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	const CD3DX12_HEAP_PROPERTIES default_heap_props{ D3D12_HEAP_TYPE_DEFAULT };

	// Fais la création avec ta classe custom de ressource (il faut la faire)
	ThrowIfFailed(device->CreateCommittedResource(
		&default_heap_props, D3D12_HEAP_FLAG_NONE,
		&buffer_desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		nullptr, IID_PPV_ARGS(&framebuffer.resource)));

	ThrowIfFailed(framebuffer.resource->SetName(L"framebuffer"));
	/* Create view for our framebuffer so compute shader can access it
	 *  passing UAV desc is not neccessary? it's determined automatically by system(correctly)
	 *  using first handle/descriptor in the heap as currently we dont have any other resources
	 */
	device->CreateUnorderedAccessView(framebuffer.Get(), nullptr, nullptr, descriptor_heap->GetCPUDescriptorHandleForHeapStart());
	++currentlyInitDescriptor;

	framebuffer.CreateOrUpdate(device.Get(), command_list.Get(), descriptor_heap.Get(), currentlyInitDescriptor, std::vector<uint32_t>{}, L"framebuffer");

	camera = Camera({ 0, 0, -10 }, { 0, 0, 1 }, { 0, 1, 0 },
		80, (float)width / (float)height, 0.1f, 100);

	CameraBuffer cameraBufferData = camera.getCameraBuffer();
	cameraBuffer.CreateOrUpdate(device.Get(), command_list.Get(), cameraBufferData,
		descriptor_heap.Get(), currentlyInitDescriptor);

	std::wstring shader_dir;
	shader_dir = std::filesystem::current_path().filename() == "bin" ? L"../src/Shaders" : L"src/Shaders";

	// Shader and its layout
	ComPtr<ID3DBlob> computeShaderBlob;
	ThrowIfFailed(CompileShaderFromFile(shader_dir + L"/ComputeShader.hlsl", "main", "cs_5_0", computeShaderBlob));

	{
		// Description des éléments de la signature racine
		CD3DX12_DESCRIPTOR_RANGE1 ranges[3];
		ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0); // Un UAV à l'emplacement 0 | framebuffer
		ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0); // camera
		ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0); // voxel map

		CD3DX12_ROOT_PARAMETER1 rootParameters[3];
		rootParameters[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_ALL);
		rootParameters[1].InitAsDescriptorTable(1, &ranges[1], D3D12_SHADER_VISIBILITY_ALL);
		rootParameters[2].InitAsDescriptorTable(1, &ranges[2], D3D12_SHADER_VISIBILITY_ALL);

		// Définir les flags de la signature racine
		D3D12_ROOT_SIGNATURE_FLAGS computeRootSignatureFlags =
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

		// Créer la signature racine
		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC computeRootSignatureDesc;
		computeRootSignatureDesc.Init_1_1(ARRAYSIZE(rootParameters), rootParameters, 0, nullptr, computeRootSignatureFlags);

		ComPtr<ID3DBlob> signature;
		ComPtr<ID3DBlob> error;

		ThrowIfFailed(D3D12SerializeVersionedRootSignature(&computeRootSignatureDesc, &signature, &error));

		ThrowIfFailed(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&root_signature)));
	}

	D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc = {};
	pso_desc.pRootSignature = root_signature.Get();
	pso_desc.CS.BytecodeLength = computeShaderBlob->GetBufferSize();
	pso_desc.CS.pShaderBytecode = computeShaderBlob->GetBufferPointer();
	ThrowIfFailed(device->CreateComputePipelineState(&pso_desc, IID_PPV_ARGS(&pso)));

	isInitialized = true;

	InitWorld();
}

void DX12ComputeContext::render()
{
	buffer_index = swapchain->GetCurrentBackBufferIndex();
	auto& backbuffer = swapchain_buffers[buffer_index];

	command_list->SetPipelineState(pso.Get());
	command_list->SetComputeRootSignature(root_signature.Get());
	command_list->SetDescriptorHeaps(1, descriptor_heap.GetAddressOf());

	// Set the root descriptor table for the UAV (at slot 0)
	// the UAV(GPU writtable buffer) is the framebuffer here
	CD3DX12_GPU_DESCRIPTOR_HANDLE framebufferHandle(descriptor_heap->GetGPUDescriptorHandleForHeapStart(), 0, device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));
	command_list->SetComputeRootDescriptorTable(0, framebufferHandle);

	// Set the root descriptor table for the CBV (at slot 1)
	// camera buffer
	CD3DX12_GPU_DESCRIPTOR_HANDLE cameraHandle(descriptor_heap->GetGPUDescriptorHandleForHeapStart(), 1, device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));
	command_list->SetComputeRootDescriptorTable(1, cameraHandle);

	//CD3DX12_GPU_DESCRIPTOR_HANDLE voxelMapHandle(descriptor_heap->GetGPUDescriptorHandleForHeapStart(), 2, device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));
	//command_list->SetComputeRootDescriptorTable(2, voxelMapHandle);

	command_list->Dispatch(threadGroupCountX, threadGroupCountY, threadGroupCountZ);


	// copy du backbuffer au framebuffer
	framebuffer.TransitionTo(command_list.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE);
	backbuffer.TransitionTo(command_list.Get(), D3D12_RESOURCE_STATE_COPY_DEST);
	command_list->CopyResource(backbuffer.Get(), framebuffer.Get());
	framebuffer.TransitionTo(command_list.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	backbuffer.TransitionTo(command_list.Get(), D3D12_RESOURCE_STATE_PRESENT);



	ThrowIfFailed(command_list->Close());
	ID3D12CommandList* const command_lists[] = { command_list.Get() };
	direct_command_queue->ExecuteCommandLists(sizeof(command_lists) / sizeof command_lists[0], command_lists);

	ThrowIfFailed(direct_command_queue->Signal(fence.Get(), ++fence_value));
	ThrowIfFailed(fence->SetEventOnCompletion(fence_value, fence_event));
	if (::WaitForSingleObject(fence_event, INFINITE) == WAIT_FAILED)
	{
		ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
	}

	ThrowIfFailed(command_allocator->Reset());
	ThrowIfFailed(command_list->Reset(command_allocator.Get(), nullptr));

	computeAndUploadCameraBuffer();

	swapchain->Present(useVSync, tearingFlag);
}

void DX12ComputeContext::flush()
{
	if (fence->GetCompletedValue() < fence_value)
	{
		fence->SetEventOnCompletion(fence_value, fence_event);
		::WaitForSingleObject(fence_event, DWORD_MAX);
	}

	ThrowIfFailed(direct_command_queue->Signal(fence.Get(), fence_value));

	ThrowIfFailed(fence->SetEventOnCompletion(fence_value, fence_event));
	::WaitForSingleObject(fence_event, INFINITE);

	for (int i = 0; i < buffer_count; ++i)
	{
		swapchain_buffers[i].resource.Reset();
	}
	swapchain.Reset();
	device.Reset();
}

void DX12ComputeContext::computeAndUploadCameraBuffer()
{
	CameraBuffer cameraBufferData = camera.getCameraBuffer();
	cameraBuffer.CreateOrUpdate(device.Get(), command_list.Get(), cameraBufferData,
		descriptor_heap.Get(), currentlyInitDescriptor);
}