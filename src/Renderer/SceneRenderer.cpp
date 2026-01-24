#include "SceneRenderer.h"
#include <cstddef>
#include <cstdint>
#include "Components/Camera_C.h"
#include "Components/Mesh_C.h"
#include "Renderer/Renderer.h"
#include "ResourceManager.h"
#include "Scene.h"
#include "Instance/InstanceManager.h"


namespace rayvox
{

void SceneRenderer::loadScene(Scene* scene)
{
    _scene = scene;
}

void SceneRenderer::uploadDirty(uint8_t frameIndex)
{
    auto& reg = _scene->_registry;
    _ctx._gpuInstanceManager->uploadRemainingFrameDirty(frameIndex);
}

void SceneRenderer::initRenderPasses()
{
    _ctx._renderer->_renderGraph->addPass(toS(RN::pass_geometry), D3D12_COMMAND_LIST_TYPE_DIRECT, 0)
        .addRecordStep(
            [this](ID3D12GraphicsCommandList* cmdList, uint32_t frameIndex)
            {
                auto* r = _ctx._renderer.get();
                auto* rM = r->_resourceManager;
                auto* psoM = r->_psoManager;
                // Render target (couleur) + Depth
                auto rtv_render3d = rM->getFrameView(RN::RTV_render_3d)[r->_frameIndex];
                // auto dsv_depth =_resourceManager->getFrameView(RN::DSV_depth)[_frameIndex];

                // 1) Transitions vers états de rendu
                rtv_render3d._resource->transitionTo(cmdList, D3D12_RESOURCE_STATE_RENDER_TARGET);
                // dsv_depth._resource->transitionTo(cmdList, D3D12_RESOURCE_STATE_DEPTH_WRITE);

                // 2) Clear
                const float clearColor[4] = {0.3f, 0.1f, 0.f, 1.f};
                cmdList->ClearRenderTargetView(rtv_render3d._descriptorHandle->cpuHandle,
                                               clearColor, 0, nullptr);

                // cmdList->ClearDepthStencilView(dsv_depth._descriptorHandle->cpuHandle,
                //                                D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

                // 3) Bind RT/DS
                // cmdList->OMSetRenderTargets(1, &rtv_render3d._descriptorHandle->cpuHandle, FALSE,
                //                             &dsv_depth._descriptorHandle->cpuHandle);

                cmdList->OMSetRenderTargets(1, &rtv_render3d._descriptorHandle->cpuHandle, FALSE,
                                            nullptr);

                D3D12_VIEWPORT vp{};
                vp.TopLeftX = 0;
                vp.TopLeftY = 0;
                vp.Width = static_cast<float>(r->_width);
                vp.Height = static_cast<float>(r->_height);
                vp.MinDepth = 0.f;
                vp.MaxDepth = 1.f;

                D3D12_RECT sc{};
                sc.left = 0;
                sc.top = 0;
                sc.right = static_cast<LONG>(r->_width);
                sc.bottom = static_cast<LONG>(r->_height);

                cmdList->RSSetViewports(1, &vp);
                cmdList->RSSetScissorRects(1, &sc);

                // 5) Descriptor heaps
                ID3D12DescriptorHeap* heaps[] = {
                    rM->_descriptorHeapAllocator_CBV_SRV_UAV.heap.Get()};
                cmdList->SetDescriptorHeaps(1, heaps);

                r->_psoManager->bindPipelineState(cmdList, toS(RN::pso_geometry_pass));

                Camera_C* cam = nullptr;
                _scene->_registry.view<Camera_C>().each(
                    [&](entt::entity e, Camera_C& c)
                    {
                        if (c._active)
                        {
                            cam = &c;
                        }
                    });
                if (!cam)
                    return;

                // auto& camBufferView =
                //     r->_resourceManager->getFrameView(cam->_buffer_ID)[r->_frameIndex];
                // camBufferView._resource->transitionTo(cmdList,
                // D3D12_RESOURCE_STATE_GENERIC_READ); cmdList->SetComputeRootDescriptorTable(0,
                //                                        camBufferView._descriptorHandle->gpuHandle);

                auto meshes = _scene->_registry.view<Mesh_C>();
                meshes.each(
                    [&](entt::entity e, Mesh_C& meshC)
                    {
                        // cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                        // cmdList->IASetVertexBuffers(0, 1, &meshC.vbview);
                        // cmdList->IASetIndexBuffer(&meshC.ibview);

                        // // Bind per-object (matrices, material, SRV, etc.)
                        // // cmdList->SetGraphicsRootConstantBufferView(1, draw.cbObjectGpu);
                        // // cmdList->SetGraphicsRootDescriptorTable(2, draw.materialSrvGpuHandle);

                        // cmdList->DrawIndexedInstanced(meshIndexCount, 1, 0, 0, 0);

                        // pour bind
                        // cmdList->IASetIndexBuffer(&ibv);
                        // D3D12_VERTEX_BUFFER_VIEW vbvs[3] = {posVBV, normalVBV, uvVBV};
                        // cmdList->IASetVertexBuffers(0, 3, vbvs);
                        // ou si on ne veux pas tout les buffer
                        // cmdList->IASetVertexBuffers(0, 1, &posVBV);
                        // cmdList->IASetVertexBuffers(2, 1, &uvVBV);
                    });

                // 9) Transition de sortie pour l’étape suivante
                // Si ta prochaine étape lit en compute / copie / SRV :
                // ou D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE si tu le samples en composition
            });

    // _ctx._renderer->_renderGraph->addPass(toS(RN::pass_render0), D3D12_COMMAND_LIST_TYPE_DIRECT,
    // 1)
    //     .addRecordStep(
    //         [this](ID3D12GraphicsCommandList* cmdList, uint32_t frameIndex)
    //         {
    //             auto* r = _ctx._renderer.get();
    //             auto uav_render0 =
    //                 r->_resourceManager->getFrameView(RN::UAV_render0)[r->_frameIndex];

    //             ID3D12DescriptorHeap* heaps[] = {
    //                 r->_resourceManager->_descriptorHeapAllocator_CBV_SRV_UAV.heap.Get()};
    //             cmdList->SetDescriptorHeaps(1, heaps);

    //             r->_psoManager->bindPipelineState(cmdList, toS(RN::pso_compute0));

    //             uav_render0._resource->transitionTo(cmdList,
    //             D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    //             Camera_C* cam = nullptr;
    //             _scene->_registry.view<Camera_C>().each(
    //                 [&](entt::entity e, Camera_C& c)
    //                 {
    //                     if (c._active)
    //                     {
    //                         cam = &c;
    //                     }
    //                 });
    //             if (!cam)
    //                 return;
    //             // auto& camBufferView =
    //             //     r->_resourceManager->getFrameView(cam->_buffer_ID)[r->_frameIndex];

    //             // camBufferView._resource->transitionTo(cmdList,
    //             D3D12_RESOURCE_STATE_GENERIC_READ);
    //             // cmdList->SetComputeRootDescriptorTable(0,
    //             uav_render0._descriptorHandle->gpuHandle);
    //             // cmdList->SetComputeRootDescriptorTable(1,
    //             // camBufferView._descriptorHandle->gpuHandle);
    //             // camera cmdList->SetComputeRootDescriptorTable(2, voxelMapView.gpuHandle);

    //             cmdList->Dispatch(r->_threadGroupCountX, r->_threadGroupCountY,
    //                               r->_threadGroupCountZ);
    //         });
}
}  // namespace rayvox
