#pragma once

#include "DX12Resource.h"
#include "includeDX12.h"
#include "AssertUtils.h"

#include <optional>

struct DX12ConstantBuffer : public DX12Resource
{
	template <typename T>
	void CreateOrUpdate(ID3D12Device* device,
		ID3D12GraphicsCommandList* commandList,
		const T& data,
		ID3D12DescriptorHeap* descriptorHeap,
		unsigned int& currentlyInitDescriptor, const wchar_t* name = L"Unnamed StructuredBuffer")
	{
		//TODO : check si sizeof(T) est un multiple de ... pour le padding

		//Constant buffers must be aligned to 256 bytes in DirectX 12
		const UINT64 bufferSize = (sizeof(T) + 255) & ~255;

		if (!resource)
		{
			// Create the constant buffer
			D3D12_HEAP_PROPERTIES heapProps = {};
			heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
			heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
			heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

			D3D12_RESOURCE_DESC bufferDesc = {};
			bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
			bufferDesc.Width = bufferSize;
			bufferDesc.Height = 1;
			bufferDesc.DepthOrArraySize = 1;
			bufferDesc.MipLevels = 1;
			bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
			bufferDesc.SampleDesc.Count = 1;
			bufferDesc.SampleDesc.Quality = 0;
			bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
			bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

			ThrowIfFailed(device->CreateCommittedResource(
				&heapProps,
				D3D12_HEAP_FLAG_NONE,
				&bufferDesc,
				D3D12_RESOURCE_STATE_GENERIC_READ,
				nullptr,
				IID_PPV_ARGS(&resource)
			));


			cpuSrvDescriptorHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(
				descriptorHeap->GetCPUDescriptorHandleForHeapStart(),
				currentlyInitDescriptor++,
				device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
			);

			D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
			cbvDesc.BufferLocation = resource->GetGPUVirtualAddress();
			cbvDesc.SizeInBytes = bufferSize;

			device->CreateConstantBufferView(&cbvDesc, cpuSrvDescriptorHandle);
		}

		// Map and update the buffer
		void* pMappedData = nullptr;
		D3D12_RANGE readRange = { 0, 0 }; // We do not intend to read from this buffer

		ThrowIfFailed(resource->Map(0, &readRange, &pMappedData));

		std::memcpy(pMappedData, &data, sizeof(T));

		resource->Unmap(0, nullptr);
		resource->SetName(name);
	}
};

struct DX12StructuredBuffer : public DX12Resource
{
	ComPtr<ID3D12Resource> uploadBuffer;

private:
	template <typename T>
	void Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* commandList,
		ID3D12DescriptorHeap* descriptorHeap, unsigned int& descriptorIndex,
		const std::vector<T>& data, const wchar_t* name, bool allowUAV = false)
	{
		UINT elementSize = sizeof(T);
		UINT bufferSize = elementSize * static_cast<UINT>(data.size());

		// Create default heap resource (GPU only)
		D3D12_HEAP_PROPERTIES defaultHeap = { D3D12_HEAP_TYPE_DEFAULT };
		D3D12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize,
			allowUAV ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE);

		ThrowIfFailed(device->CreateCommittedResource(
			&defaultHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&resource)
		));

		// Create upload buffer (CPU -> GPU)
		D3D12_HEAP_PROPERTIES uploadHeap = { D3D12_HEAP_TYPE_UPLOAD };
		D3D12_RESOURCE_DESC uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);

		ThrowIfFailed(device->CreateCommittedResource(
			&uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadBuffer)
		));

		// Copy data to upload buffer
		void* mappedData;
		ThrowIfFailed(uploadBuffer->Map(0, nullptr, &mappedData));
		memcpy(mappedData, data.data(), bufferSize);
		uploadBuffer->Unmap(0, nullptr);

		// Copy from upload buffer to GPU buffer
		commandList->CopyResource(resource.Get(), uploadBuffer.Get());

		// Create SRV (for reading in shaders)
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Buffer.FirstElement = 0;
		srvDesc.Buffer.NumElements = static_cast<UINT>(data.size());
		srvDesc.Buffer.StructureByteStride = elementSize;

		// Get the CPU descriptor handle for the SRV
		cpuSrvDescriptorHandle = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
		cpuSrvDescriptorHandle.ptr += descriptorIndex * device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		// Create the SRV using the CPU descriptor handle
		device->CreateShaderResourceView(resource.Get(), &srvDesc, cpuSrvDescriptorHandle);

		// Store the GPU descriptor handle for binding in shaders
		gpuSrvDescriptorHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(
			descriptorHeap->GetGPUDescriptorHandleForHeapStart(),
			descriptorIndex,
			device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
		);

		// Increment the descriptor index
		descriptorIndex++;

		resource->SetName(name);
	}

	template <typename T>
	void UpdateData(ID3D12Device* device, ID3D12GraphicsCommandList* commandList, const std::vector<T>& newData)
	{
		UINT bufferSize = sizeof(T) * static_cast<UINT>(newData.size());

		// Ensure the upload buffer exists
		if (!uploadBuffer)
		{
			D3D12_HEAP_PROPERTIES uploadHeap = { D3D12_HEAP_TYPE_UPLOAD };
			D3D12_RESOURCE_DESC uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);

			ThrowIfFailed(device->CreateCommittedResource(
				&uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadBuffer)
			));
		}

		// Map and copy new data to the upload buffer
		void* mappedData;
		ThrowIfFailed(uploadBuffer->Map(0, nullptr, &mappedData));
		memcpy(mappedData, newData.data(), bufferSize);
		uploadBuffer->Unmap(0, nullptr);

		// Schedule a copy from the upload buffer to the GPU buffer
		commandList->CopyResource(resource.Get(), uploadBuffer.Get());
	}

public:
	template <typename T>
	void CreateOrUpdate(ID3D12Device* device, ID3D12GraphicsCommandList* commandList,
		ID3D12DescriptorHeap* descriptorHeap, unsigned int& descriptorIndex,
		const std::vector<T>& data, const wchar_t* name = L"Unnamed StructuredBuffer", bool allowUAV = false)
	{
		UINT bufferSize = sizeof(T) * static_cast<UINT>(data.size());
		if (resource && resource->GetDesc().Width >= bufferSize)
		{
			UpdateData(device, commandList, data);
			return;
		}
		Initialize(device, commandList, descriptorHeap, descriptorIndex, data, name, allowUAV);
	}
};

struct DX12UnorderedAccessBuffer : public DX12Resource
{
	ComPtr<ID3D12Resource> uploadBuffer;

public:
	template <typename T>
	void Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* commandList,
		ID3D12DescriptorHeap* descriptorHeap, D3D12_RESOURCE_DESC buffer_desc, unsigned int& descriptorIndex,
		const std::vector<T>& data, const wchar_t* name)
	{
		UINT elementSize = sizeof(T);
		UINT bufferSize = elementSize * static_cast<UINT>(data.size());
		
		bool isTexture2D = (buffer_desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D);

		// Create default heap resource (GPU only) with UAV flag
		D3D12_HEAP_PROPERTIES defaultHeap = { D3D12_HEAP_TYPE_DEFAULT };
		buffer_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

		ThrowIfFailed(device->CreateCommittedResource(
			&defaultHeap, D3D12_HEAP_FLAG_NONE, &buffer_desc, 
			isTexture2D ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : D3D12_RESOURCE_STATE_COPY_DEST, 
			nullptr, IID_PPV_ARGS(&resource)
		));

		// Handle upload buffer only if it's NOT a texture (structured buffers need CPU data upload)
		if (!isTexture2D)
		{
			D3D12_HEAP_PROPERTIES uploadHeap = { D3D12_HEAP_TYPE_UPLOAD };
			D3D12_RESOURCE_DESC uploadBufferDesc = buffer_desc;
			uploadBufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

			ThrowIfFailed(device->CreateCommittedResource(
				&uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadBufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadBuffer)
			));

			// Copy data to upload buffer
			void* mappedData;
			ThrowIfFailed(uploadBuffer->Map(0, nullptr, &mappedData));
			memcpy(mappedData, data.data(), bufferSize);
			uploadBuffer->Unmap(0, nullptr);

			// Copy from upload buffer to GPU buffer
			commandList->CopyResource(resource.Get(), uploadBuffer.Get());
		}

		// Create UAV (for read-write access in shaders)
		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.Format = DXGI_FORMAT_UNKNOWN;

		if (isTexture2D)
		{
			uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
			uavDesc.Texture2D.MipSlice = 0;
		}
		else
		{
			uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
			uavDesc.Buffer.FirstElement = 0;
			uavDesc.Buffer.NumElements = static_cast<UINT>(data.size());
			uavDesc.Buffer.StructureByteStride = elementSize;
			uavDesc.Buffer.CounterOffsetInBytes = 0;
		}

		// Get the CPU descriptor handle for the UAV
		cpuSrvDescriptorHandle = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
		cpuSrvDescriptorHandle.ptr += descriptorIndex * device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		// Create the UAV using the CPU descriptor handle
		device->CreateUnorderedAccessView(resource.Get(), nullptr, &uavDesc, cpuSrvDescriptorHandle);

		// Store the GPU descriptor handle for binding in shaders
		gpuSrvDescriptorHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(
			descriptorHeap->GetGPUDescriptorHandleForHeapStart(),
			descriptorIndex,
			device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
		);

		// Increment the descriptor index
		descriptorIndex++;

		resource->SetName(name);

		init = true;
	}

	template <typename T>
	void UpdateData(ID3D12Device* device, ID3D12GraphicsCommandList* commandList, const std::vector<T>& newData)
	{
		UINT bufferSize = sizeof(T) * static_cast<UINT>(newData.size());

		// Ensure the upload buffer exists
		if (!uploadBuffer)
		{
			D3D12_HEAP_PROPERTIES uploadHeap = { D3D12_HEAP_TYPE_UPLOAD };
			D3D12_RESOURCE_DESC uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);

			ThrowIfFailed(device->CreateCommittedResource(
				&uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadBuffer)
			));
		}

		// Map and copy new data to the upload buffer
		void* mappedData;
		ThrowIfFailed(uploadBuffer->Map(0, nullptr, &mappedData));
		memcpy(mappedData, newData.data(), bufferSize);
		uploadBuffer->Unmap(0, nullptr);

		// Schedule a copy from the upload buffer to the GPU buffer
		commandList->CopyResource(resource.Get(), uploadBuffer.Get());
	}
	
};