#include "Renderer.h"
#include "AssertUtils.h"
#include "../VoxelDataStructs.h"
#include "CommandQueue.h"
#include "DirectX-Headers/include/directx/d3d12.h"

#include <glm/glm.hpp>
#include <filesystem>
#include <optional>
#include <string>

namespace RayVox {
	void Renderer::InitWorld()
	{
		SparseGrid sg;
		sg.DEBUG_fill();
		//voxelMap = sg.getAllVoxels();
	
		//voxelMapBuffer.CreateOrUpdate(device.Get(), command_list.Get(),
		//	descriptor_heap.Get(), currentlyInitDescriptor, voxelMap);
	
	}
	
	bool Renderer::setTearingFlag()
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
	
	HRESULT Renderer::CompileShaderFromFile(const std::wstring& filename, const std::string& entryPoint,
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
	
	void Renderer::init(HWND hWnd, uint32_t clientWidth, uint32_t clientHeight)
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
	
		ThrowIfFailed(CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(&dxgi_factory)));

		ComPtr<IDXGIAdapter1> adapter;
		for (UINT i = 0; ; ++i) {
			ComPtr<IDXGIAdapter1> temp;
			if (dxgi_factory->EnumAdapters1(i, &temp) == DXGI_ERROR_NOT_FOUND)
				break;

			DXGI_ADAPTER_DESC1 desc;
			temp->GetDesc1(&desc);

			if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue; // skip WARP
			adapter = temp;  // premier GPU dédié trouvé
			std::wcout << L"Selected GPU : " << desc.Description << std::endl;
			break;
		}
		// Passing nullptr means its up to system which adapter to use for device, might even use WARP(no gpu)
		ThrowIfFailed(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));
		
		
		descriptorHeapAllocator_CBV_SRV_UAV.init(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 128);
		descriptorHeapAllocator_SAMPLER.init(device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 8);
		descriptorHeapAllocator_RTV.init(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1);
		descriptorHeapAllocator_DSV.init(device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1);
		
		CommandQueues.emplace_back(device, D3D12_COMMAND_LIST_TYPE_DIRECT, swapChain_buffer_count);
	
		setTearingFlag();
	
		
		const DXGI_SWAP_CHAIN_DESC1 swapchain_desc = {
			width,
			height,
			DXGI_FORMAT_R8G8B8A8_UNORM,
			FALSE,
			{1, 0},
			DXGI_USAGE_BACK_BUFFER,
			swapChain_buffer_count,
			DXGI_SCALING_STRETCH,
			DXGI_SWAP_EFFECT_FLIP_DISCARD,
			DXGI_ALPHA_MODE_UNSPECIFIED,
			tearingFlag };
		// Create and upgrade swapchain to our version(ComPtr<IDXGISwapChain4> swapchain)
		ComPtr<IDXGISwapChain1> swapchain_tier_dx12;
		ThrowIfFailed(dxgi_factory->CreateSwapChainForHwnd(
			CommandQueues[0].commandQueue.Get(),
			hWnd,
			&swapchain_desc,
			nullptr,
			nullptr,
			&swapchain_tier_dx12));
		ThrowIfFailed(swapchain_tier_dx12.As(&swapchain));
	
		// Get swapchain pointers to ID3D12Resource's that represents buffers
		for (int i = 0; i < swapChain_buffer_count; i++)
		{
			ThrowIfFailed(swapchain->GetBuffer(i, IID_PPV_ARGS(&swapchain_buffers[i].resource)));
			auto name = "swapchain_buffer_" + std::to_string(i);
			std::wstring wname(name.begin(), name.end());
			ThrowIfFailed(swapchain_buffers[i].resource->SetName(wname.c_str()));
		}
	
		// Retrieve swapchain buffer description and create identical resource but with UAV allowed, so compute shader could write to it
		auto buffer_desc = swapchain_buffers[0].resource->GetDesc();
		//framebuffer.Initialize(device.Get(), command_list.Get(), descriptor_heap.Get(), buffer_desc, currentlyInitDescriptor, std::vector<uint32_t>{}, L"framebuffer");
	
		camera = Camera({ 0, 0, -10 }, { 0, 0, 1 }, { 0, 1, 0 },
			80, (float)width / (float)height, 0.1f, 100);
	
		CameraBuffer cameraBufferData = camera.getCameraBuffer();
		//cameraBuffer.CreateOrUpdate(device.Get(), command_list.Get(), cameraBufferData, descriptor_heap.Get(), currentlyInitDescriptor);
	
		std::wstring shader_dir;
		shader_dir = std::filesystem::current_path().filename() == "build" ? L"../src/Shaders" : L"src/Shaders";
	
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
	
	void Renderer::render()
	{
		buffer_index = swapchain->GetCurrentBackBufferIndex();
		auto& backbuffer = swapchain_buffers[buffer_index];
	
		for(CommandQueue& queue : CommandQueues){
			CommandQueue::Command& cmd = queue.getCommand(buffer_index);
			if(queue.isCommandComplete(cmd)){
				cmd.commandAllocator->Reset();
        		cmd.commandList->Reset(cmd.commandAllocator.Get(), nullptr);
				// pass de rendu, enregistrement des commands dans la command list
				queue.executeCommand(buffer_index);
			}
		}
	
		swapchain->Present(useVSync, tearingFlag);
	}
	
	void Renderer::flush()
	{
		for(auto& q : CommandQueues){
			q.flush();
		}
	
		for (int i = 0; i < swapChain_buffer_count; ++i)
		{
			swapchain_buffers[i].resource.Reset();
		}
		swapchain.Reset();
		device.Reset();
	}
	
	void Renderer::computeAndUploadCameraBuffer()
	{
		CameraBuffer cameraBufferData = camera.getCameraBuffer();
		// cameraBuffer.CreateOrUpdate(device.Get(), command_list.Get(), cameraBufferData,
		// 	descriptor_heap.Get(), currentlyInitDescriptor);
	}
}