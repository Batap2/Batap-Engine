#pragma once

#include "SceneRenderer.h"
#include "Components/ComponentFlags.h"
#include "Components/ComponentTraits.h"
#include "ResourceManager.h"
#include "Scene.h"

namespace rayvox
{

// TODO : j'étais en train de me demander si faire GPUComponent c'était bien, parce que quand on
// rajoute un component il faut penser a le rajouter ici
// le but et de recup une view de tout les component GPU et d'upload si dirty
using GPUComponent = std::tuple<Camera_C>;

void SceneRenderer::loadScene(Scene* scene)
{
    _scene = scene;
}

void SceneRenderer::uploadDirty()
{
}

void SceneRenderer::initRenderPasses()
{
    _renderer->_renderGraph->addPass(toS(RN::pass_render0), D3D12_COMMAND_LIST_TYPE_DIRECT, 0)
        .addRecordStep(
            [this](ID3D12GraphicsCommandList* cmdList, uint32_t frameIndex)
            {
                auto* r = _renderer;
                auto uav_render0 =
                    r->_resourceManager->getFrameView(RN::UAV_render0)[r->_buffer_index];

                ID3D12DescriptorHeap* heaps[] = {
                    r->_resourceManager->_descriptorHeapAllocator_CBV_SRV_UAV.heap.Get()};
                cmdList->SetDescriptorHeaps(1, heaps);

                r->_psoManager->bindPipelineState(cmdList, toS(RN::pso_compute0));

                uav_render0._resource->transitionTo(cmdList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

                cmdList->SetComputeRootDescriptorTable(
                    0, uav_render0._descriptorHandle->gpuHandle);  // UAV framebuffer
                // cmdList->SetComputeRootDescriptorTable(1, cameraBufferView.gpuHandle); // CBV
                // camera cmdList->SetComputeRootDescriptorTable(2, voxelMapView.gpuHandle); // SRV
                // voxel map

                cmdList->Dispatch(r->_threadGroupCountX, r->_threadGroupCountY,
                                  r->_threadGroupCountZ);
            });
}
}  // namespace rayvox