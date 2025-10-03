#pragma once

#include "CommandQueue.h"
#include "DirectX-Headers/include/directx/d3d12.h"
#include "includeDX12.h"
#include "../Camera.h"
#include "DX12Buffers.h"
#include "../VoxelDataStructs.h"
#include "DescriptorHeapAllocator.h"
#include <vector>

namespace RayVox {
	struct Renderer
	{
		uint32_t threadGroupSizeX = 8;
		uint32_t threadGroupSizeY = 8;
	
		bool useVSync = false;
		UINT tearingFlag = 0;
		bool fullscreen = false;
	
		// DXGI
		ComPtr<IDXGIFactory4> dxgi_factory;
		ComPtr<IDXGISwapChain4> swapchain;
		static const uint32_t swapChain_buffer_count = 3;
		uint32_t buffer_index = 0;
	
		// D3D12 core interfaces
		ComPtr<ID3D12Debug6> debug_controller;
		ComPtr<ID3D12Device2> device;

		// Command interfaces
		std::vector<CommandQueue> CommandQueues;
	
		// GPU Resources
		DX12Resource swapchain_buffers[swapChain_buffer_count];
		DX12UnorderedAccessBuffer framebuffer;
	
		// Shader layout and pipeline state
		ComPtr<ID3D12RootSignature> root_signature;
		ComPtr<ID3D12PipelineState> pso;
	
		// Resource descriptors(views)
		DescriptorHeapAllocator descriptorHeapAllocator_CBV_SRV_UAV;
		DescriptorHeapAllocator descriptorHeapAllocator_SAMPLER;
		DescriptorHeapAllocator descriptorHeapAllocator_RTV;
		DescriptorHeapAllocator descriptorHeapAllocator_DSV;

	
		unsigned int currentlyInitDescriptor = 0;
	
		bool isInitialized = false;
	
		uint32_t width, height;
	
		uint32_t threadGroupCountX, threadGroupCountY, threadGroupCountZ;
	
		void computeAndUploadCameraBuffer();
	
		bool setTearingFlag();
	
		HRESULT CompileShaderFromFile(const std::wstring& filename, const std::string& entryPoint,
			const std::string& target, ComPtr<ID3DBlob>& shaderBlob);
	
		void flush();
	
		void init(HWND hWnd, uint32_t clientWidth, uint32_t clientHeight);
	
		void render();
	
		// Il va falloir scinder les truc du haut qui appartiennent au Renderer et les truc du bas, des objets
		// basique, camera, les voxels etc
	
		Camera camera;
		DX12ConstantBuffer cameraBuffer;
	
		std::vector<Voxel> voxelMap;
		DX12StructuredBuffer voxelMapBuffer;
	
	
		void InitWorld();
	};
}