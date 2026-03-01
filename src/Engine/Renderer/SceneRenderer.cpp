#include "SceneRenderer.h"

#include <cstdint>
#include "Assets/AssetManager.h"
#include "Assets/Mesh.h"
#include "Components/Camera_C.h"
#include "Components/EntityHandle.h"
#include "Components/Mesh_C.h"
#include "Components/RenderInstanceID_C.h"
#include "Components/Transform_C.h"
#include "Instance/InstanceManager.h"
#include "Renderer/Renderer.h"
#include "ResourceManager.h"
#include "Scene.h"

namespace batap
{
void SceneRenderer::uploadDirty()
{
    args_.instanceManager_->uploadRemainingFrameDirty(_ctx);
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
                auto& assetM = _ctx._assetManager;

                auto&& [reg, instanceM] = args_;

                auto rtv_render3d = rM->getFrameView(RN::RTV_render_3d)[r->_frameIndex];
                auto dsv_depth = rM->getFrameView(RN::DSV_render_3d)[r->_frameIndex];

                rtv_render3d._resource->transitionTo(cmdList, D3D12_RESOURCE_STATE_RENDER_TARGET);
                dsv_depth._resource->transitionTo(cmdList, D3D12_RESOURCE_STATE_DEPTH_WRITE);

                const float clearColor[4] = {0.2f, 0.2f, 0.2f, 1.f};
                cmdList->ClearRenderTargetView(rtv_render3d._descriptorHandle->cpuHandle,
                                               clearColor, 0, nullptr);

                cmdList->ClearDepthStencilView(dsv_depth._descriptorHandle->cpuHandle,
                                               D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

                cmdList->OMSetRenderTargets(1, &rtv_render3d._descriptorHandle->cpuHandle, FALSE,
                                            &dsv_depth._descriptorHandle->cpuHandle);

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

                ID3D12DescriptorHeap* heaps[] = {
                    rM->_descriptorHeapAllocator_CBV_SRV_UAV.heap.Get()};
                cmdList->SetDescriptorHeaps(1, heaps);

                r->_psoManager->bindPipelineState(cmdList, toS(RN::pso_geometry_pass));

                EntityHandle cam;
                reg->view<Camera_C, Transform_C>().each(
                    [&](entt::entity e, Camera_C& c, Transform_C& t)
                    {
                        if (c._active)
                        {
                            cam = {reg, e};
                        }
                    });
                if (!cam.valid())
                    return;

                auto camSRVHandle = r->_resourceManager->getFrameView(
                    instanceM->_cameraInstancesPool._instancePoolViewHandle)[frameIndex];
                auto meshesSRVHandle = r->_resourceManager->getFrameView(
                    instanceM->_meshInstancesPool._instancePoolViewHandle)[frameIndex];

                cmdList->SetGraphicsRootDescriptorTable(0,
                                                        camSRVHandle._descriptorHandle->gpuHandle);
                cmdList->SetGraphicsRootDescriptorTable(
                    1, meshesSRVHandle._descriptorHandle->gpuHandle);

                uint32_t bindedConstants[2] = {instanceM->_cameraInstancesPool.get(cam)._gpuIndex,
                                               0};

                auto meshes = reg->view<Mesh_C, RenderInstance_C>();
                meshes.each(
                    [&](entt::entity e, Mesh_C& meshC, RenderInstance_C& renderInstanceC)
                    {
                        if (!meshC._mesh)
                            return;
                        auto* mesh = assetM->get(meshC._mesh);

                        cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

                        bindedConstants[1] = renderInstanceC._instanceID;

                        cmdList->SetGraphicsRoot32BitConstants(2, 2, bindedConstants, 0);

                        auto& ib = rM->getStaticMeshView(mesh->_indexBuffer);
                        auto& vb = rM->getStaticMeshView(mesh->_vertexBuffer);
                        auto& nb = rM->getStaticMeshView(mesh->_normalBuffer);
                        auto& uvb = rM->getStaticMeshView(mesh->_uv0Buffer);

                        D3D12_VERTEX_BUFFER_VIEW vbvs[3] = {vb.vbv, nb.vbv, uvb.vbv};

                        cmdList->IASetIndexBuffer(&ib.ibv);
                        cmdList->IASetVertexBuffers(0, 3, vbvs);

                        cmdList->DrawIndexedInstanced(mesh->_indexCount, 1, 0, 0, 0);
                    });
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
}  // namespace batap
