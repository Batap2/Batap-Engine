#pragma once

#include <wrl.h>
using namespace Microsoft::WRL;

#include "Descriptorhandle.h"

#include <DirectX-Headers/include/directx/d3dx12.h>

#include <unordered_map>
#include <string>

namespace RayVox{
    struct GPUResource{
        GPUResource() {}

        ComPtr<ID3D12Resource> resource;
        bool init = false;

        void TransitionTo(ComPtr<ID3D12GraphicsCommandList> commandList, D3D12_RESOURCE_STATES newState)
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
    };

    struct GPUView : GPUResource{
        DescriptorHandle descriptorHandle;
        enum class Type{CBV, SRV, UAV, RTV, DSV} type;
    };

    struct ResourceManager{
        // Dynamic resources: duplicated per frame (e.g., constant buffers)
        std::unordered_map<std::string, std::vector<GPUView>> dynamicResources;

        // Static resources: rarely updated, must wait for GPU before re-upload
        std::unordered_map<std::string, GPUView> staticResources;
    };
}