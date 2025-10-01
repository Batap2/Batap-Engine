#pragma once

#include <wrl.h>
using namespace Microsoft::WRL;

#include <DirectX-Headers/include/directx/d3dx12.h>

struct DX12Resource {
public:
	DX12Resource() {}

	ComPtr<ID3D12Resource> resource;
	bool init = false;

	void TransitionTo(ID3D12GraphicsCommandList* commandList, D3D12_RESOURCE_STATES newState)
	{
		if (currentState != newState)
		{
			CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(resource.Get(), currentState, newState);
			commandList->ResourceBarrier(1, &barrier);
			currentState = newState;
		}
	}

	ID3D12Resource* Get() {
		return resource.Get();
	}
protected:
	D3D12_RESOURCE_STATES currentState = D3D12_RESOURCE_STATE_COMMON;
	D3D12_GPU_DESCRIPTOR_HANDLE gpuSrvDescriptorHandle = {};
	D3D12_CPU_DESCRIPTOR_HANDLE cpuSrvDescriptorHandle = {};
};