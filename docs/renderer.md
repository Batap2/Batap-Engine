# Renderer

## CommandQueue

**Purpose**

- Own a DX12 command queue
- Manage a fixed set of command allocators and command lists
- Execute command lists and signal GPU fences

**Properties**

- One queue per command list type
- One `Command` per frame-in-flight
- Each `Command` owns its allocator and command list

---

## DescriptorHeapAllocator

DX12 descriptor heap allocation and recycling.

**Purpose**

- Own a DX12 descriptor heap
- Allocate and recycle descriptor slots
- Provide CPU/GPU descriptor handles

**Properties**

- One allocator per descriptor heap type (CBV/SRV/UAV, Sampler)
- Fixed-capacity descriptor heap
- Linear allocation with free-list recycling

**DescriptorHandle**

- One handle per allocated descriptor
- Stores heap index
- Stores CPU handle
- Stores GPU handle when shader-visible

---

## FenceManager

- DX12 fence management and CPU/GPU synchronization.

---

## ResourceManager

GPU resource + view lifetime on DX12. Creation, uploads, and deferred release.

**Purpose**

- Create GPU buffers/textures (static or per-frame)
- Create GPU views (CBV/SRV/UAV/RTV/DSV) + mesh views (VBV/IBV)
- Upload CPU data to GPU via per-frame upload buffers
- Defer resource/view destruction until GPU is done

**Properties**

- Static resources: Rarely changing resource
- Frame resources: Constantly changing. Duplicated for all frames in flight
- Views mirror resource lifetime (StaticView / FrameView)
- Per-frame `UploadBuffer` (UPLOAD heap, mapped CPU)
- One internal fence (`Fence_ResourceManager`) for upload/release sync

**Upload**

- `requestUpload()` queues external data pointer
- `requestUploadOwned()` returns a writable span stored in the request
- `flushUploadRequests()` uploads all queued requests then resets upload offset

**Descriptor heaps**

- Owns 4 `DescriptorHeapAllocator`:
- CBV/SRV/UAV, Sampler, RTV, DSV

**Destruction**

- `requestDestroy()` removes from active maps and pushes into `_deferredReleases`
- `flushDeferredReleases()` stamps pending entries with a fence value and releases when complete

---

## PipelineStateManager

**Purpose**

- Create Root Signatures from a small description
- Create and cache PSOs (Graphics / Compute)
- Bind PSOs with redundant-bind avoidance

**Properties**

- Stores PSOs and Shaders
- Root signature params:
  - Descriptor table (`DescriptorTableDesc`)
  - Root constants (`RootConstantsDesc`)

**exemple : creation of a 3D pipeline state**

```cpp
{
        auto vs = _psoManager->compileShaderFromFile(
            toS("shader_3D_VS"), shader_dir + "/VertexShader.hlsl", "main", "vs_5_1");

        auto ps = _psoManager->compileShaderFromFile(
            toS("shader_3D_PS"), shader_dir + "/PixelShader.hlsl", "main", "ps_5_1");

        RootSignatureDescription rsDesc_VS{
            {
                DescriptorTableDesc{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0,
                                    D3D12_SHADER_VISIBILITY_ALL},            // Camera
                DescriptorTableDesc{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0,
                                    D3D12_SHADER_VISIBILITY_VERTEX},         // Mesh InstanceData
                RootConstantsDesc{2, 0, 0, D3D12_SHADER_VISIBILITY_ALL}      // indices : Camera, Mesh
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
            "geometry_pass", rsDesc_VS,
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
```

---

## SceneRenderer

Decouples rendering from the Scene.

**Purpose**

- Bind a `Scene` to the renderer side
- Define render passes for the current scene
- Trigger GPU instance uploads for dirty ECS data

**Properties**

- Holds a `Scene*` (no ownership)
- Uses `Context` to access renderer, assets, and instance manager
- Passes are registered into the `RenderGraph`

**Rules**

- Scene logic stays in ECS systems, not here
- Only consumes ECS state to render (camera selection, mesh instances)
- Uploads go through `GPUInstanceManager` (per frame index)

---

## RenderGraph

**Purpose**

- Organize render passes
- Record and execute per frame-in-flight

**Properties**

- Pass = list of record steps
- One command list per queue per frame
