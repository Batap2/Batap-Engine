#pragma once

#include "SceneRenderer.h"
#include "Components/Camera_C.h"
#include "Components/ComponentTraits.h"
#include "ResourceManager.h"
#include "Scene.h"
#include "VoxelRayScene.h"

namespace rayvox
{

void SceneRenderer::loadScene(Scene* scene)
{
    _scene = scene;
}

void SceneRenderer::uploadDirty()
{
    using GPUComponent = std::tuple<Camera_C /*, Material_C, Light_C, ...*/>;

    auto& reg = _scene->_registry;

    auto processTupleAll = [&]<typename... Ts>(std::type_identity<std::tuple<Ts...>>)
    {
        auto view = reg.view<Ts...>();
        for (auto e : view)
        {
            (ComponentTraits<Ts>::upload(*_renderer->_resourceManager, reg.get<Ts>(e),
                                         _renderer->_frameIndex),
             ...);
        }
    };

    processTupleAll(std::type_identity<GPUComponent>{});
}

void SceneRenderer::initRenderPasses()
{
    _renderer->_renderGraph->addPass(toS(RN::pass_render0), D3D12_COMMAND_LIST_TYPE_DIRECT, 0)
        .addRecordStep(
            [this](ID3D12GraphicsCommandList* cmdList, uint32_t frameIndex)
            {
                auto* r = _renderer;
                auto uav_render0 =
                    r->_resourceManager->getFrameView(RN::UAV_render0)[r->_frameIndex];

                ID3D12DescriptorHeap* heaps[] = {
                    r->_resourceManager->_descriptorHeapAllocator_CBV_SRV_UAV.heap.Get()};
                cmdList->SetDescriptorHeaps(1, heaps);

                r->_psoManager->bindPipelineState(cmdList, toS(RN::pso_compute0));

                uav_render0._resource->transitionTo(cmdList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

                VoxelRayScene* scene = static_cast<VoxelRayScene*>(_scene);
                auto* cam = _scene->_registry.try_get<Camera_C>(scene->_camera);
                auto& camBufferView = r->_resourceManager->getFrameView(cam->_buffer_ID)[r->_frameIndex];

                camBufferView._resource->transitionTo(cmdList, D3D12_RESOURCE_STATE_GENERIC_READ);
                cmdList->SetComputeRootDescriptorTable(
                    0, uav_render0._descriptorHandle->gpuHandle);
                cmdList->SetComputeRootDescriptorTable(1, camBufferView._descriptorHandle->gpuHandle);
                // camera cmdList->SetComputeRootDescriptorTable(2, voxelMapView.gpuHandle);

                cmdList->Dispatch(r->_threadGroupCountX, r->_threadGroupCountY,
                                  r->_threadGroupCountZ);
            });
}
}  // namespace rayvox