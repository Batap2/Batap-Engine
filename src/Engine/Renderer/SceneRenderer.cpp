#include "SceneRenderer.h"

#include <cstdint>
#include "Assets/AssetManager.h"
#include "Assets/Material.h"
#include "Assets/Mesh.h"
#include "Assets/Texture.h"
#include "Components/Camera_C.h"
#include "Components/EntityHandle.h"
#include "Components/Mesh_C.h"
#include "Components/RenderInstanceID_C.h"
#include "Components/Skybox_C.h"
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

                if(!reg) return;

                auto rtv_render3d = rM->getFrameView(RN::RTV_render_3d)[r->_frameIndex];
                auto dsv_depth    = rM->getFrameView(RN::DSV_render_3d)[r->_frameIndex];
                auto rtv_normalRT = rM->getFrameView(RN::RTV_normalRT)[r->_frameIndex];

                rtv_render3d._resource->transitionTo(cmdList, D3D12_RESOURCE_STATE_RENDER_TARGET);
                dsv_depth._resource->transitionTo(cmdList, D3D12_RESOURCE_STATE_DEPTH_WRITE);
                rtv_normalRT._resource->transitionTo(cmdList, D3D12_RESOURCE_STATE_RENDER_TARGET);

                const float clearColor[4]  = {0.0f, 0.0f, 0.0f, 0.0f};
                const float clearNormal[4] = {0.5f, 0.5f, 1.0f, 0.0f};
                cmdList->ClearRenderTargetView(rtv_render3d._descriptorHandle->cpuHandle,
                                               clearColor, 0, nullptr);
                cmdList->ClearRenderTargetView(rtv_normalRT._descriptorHandle->cpuHandle,
                                               clearNormal, 0, nullptr);

                cmdList->ClearDepthStencilView(dsv_depth._descriptorHandle->cpuHandle,
                                               D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

                D3D12_CPU_DESCRIPTOR_HANDLE rtvs[] = {
                    rtv_render3d._descriptorHandle->cpuHandle,
                    rtv_normalRT._descriptorHandle->cpuHandle};
                cmdList->OMSetRenderTargets(2, rtvs, FALSE,
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
                    instanceM->pool<CameraInstance>()._instancePoolViewHandle)[frameIndex];
                auto meshesSRVHandle = r->_resourceManager->getFrameView(
                    instanceM->pool<StaticMeshInstance>()._instancePoolViewHandle)[frameIndex];
                auto pointLightSRVHandle = r->_resourceManager->getFrameView(
                    instanceM->pool<PointLightInstance>()._instancePoolViewHandle)[frameIndex];

                auto matSrvHandle = _ctx._assetManager->getGPUArena<Material>()->srvHandle();
                auto& matSrv      = rM->getStaticView(matSrvHandle);

                auto skyboxSRVHandle = rM->getFrameView(
                    instanceM->pool<SkyboxInstance>()._instancePoolViewHandle)[frameIndex];

                camSRVHandle._resource->transitionTo(cmdList, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
                meshesSRVHandle._resource->transitionTo(cmdList, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
                pointLightSRVHandle._resource->transitionTo(cmdList, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
                matSrv._resource->transitionTo(cmdList, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
                skyboxSRVHandle._resource->transitionTo(cmdList, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);

                cmdList->SetGraphicsRootDescriptorTable(0,
                                                        camSRVHandle._descriptorHandle->gpuHandle);
                cmdList->SetGraphicsRootDescriptorTable(
                    1, meshesSRVHandle._descriptorHandle->gpuHandle);
                cmdList->SetGraphicsRootDescriptorTable(2,
                                                        pointLightSRVHandle._descriptorHandle->gpuHandle);
                cmdList->SetGraphicsRootDescriptorTable(4,
                                                        matSrv._descriptorHandle->gpuHandle);
                cmdList->SetGraphicsRootDescriptorTable(
                    5, rM->_descriptorHeapAllocator_CBV_SRV_UAV.heap->GetGPUDescriptorHandleForHeapStart());
                cmdList->SetGraphicsRootDescriptorTable(6, skyboxSRVHandle._descriptorHandle->gpuHandle);

                auto camID = instanceM->pool<CameraInstance>().getGPUIndex(cam);
                if (!camID.valid())
                    return;

                uint32_t bindedConstants[3] = {camID, 0, static_cast<uint32_t>(instanceM->pool<PointLightInstance>().size())};

                auto meshes = reg->view<Mesh_C>();
                meshes.each(
                    [&](entt::entity e, Mesh_C& meshC)
                    {
                        if (!meshC._mesh)
                            return;
                        auto* mesh = assetM->get(meshC._mesh);

                        cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

                        auto id = instanceM->pool<StaticMeshInstance>().getGPUIndex({reg, e});
                        if (!id.valid())
                            return;

                        bindedConstants[1] = id;

                        cmdList->SetGraphicsRoot32BitConstants(3, 3, bindedConstants, 0);

                        auto& ib  = rM->getStaticMeshView(mesh->_indexBuffer);
                        auto& vb  = rM->getStaticMeshView(mesh->_vertexBuffer);
                        auto& nb  = rM->getStaticMeshView(mesh->_normalBuffer);
                        auto& uvb = rM->getStaticMeshView(mesh->_uv0Buffer);
                        auto& tb  = rM->getStaticMeshView(mesh->_tangeantBuffer);

                        D3D12_VERTEX_BUFFER_VIEW vbvs[4] = {vb.vbv, nb.vbv, uvb.vbv, tb.vbv};

                        cmdList->IASetIndexBuffer(&ib.ibv);
                        cmdList->IASetVertexBuffers(0, 4, vbvs);

                        cmdList->DrawIndexedInstanced(mesh->_indexCount, 1, 0, 0, 0);
                    });
            });

    // --- Sky pass (index 1 : after geometry, before composition) ---
    _ctx._renderer->_renderGraph->addPass(toS(RN::pass_sky), D3D12_COMMAND_LIST_TYPE_DIRECT, 1)
        .addRecordStep(
            [this](ID3D12GraphicsCommandList* cmdList, uint32_t frameIndex)
            {
                auto* r  = _ctx._renderer.get();
                auto* rM = r->_resourceManager;
                auto&& [reg, instanceM] = args_;

                if(!reg) return;

                // Find Skybox_C component
                Skybox_C* sky = nullptr;
                reg->view<Skybox_C>().each([&](entt::entity, Skybox_C& s) { sky = &s; });
                if (!sky)
                    return;

                // En mode HDRI la texture doit être disponible
                Texture* tex = nullptr;
                if (sky->mode_ == Skybox_C::Mode::HDRI)
                {
                    if (!sky->hdri_)
                        return;
                    tex = _ctx._assetManager->get<Texture>(sky->hdri_);
                    if (!tex)
                        return;
                    rM->getStaticView(tex->viewHandle_)._resource->transitionTo(
                        cmdList, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
                }

                auto rtv = rM->getFrameView(RN::RTV_render_3d)[r->_frameIndex];
                auto dsv = rM->getFrameView(RN::DSV_render_3d)[r->_frameIndex];

                rtv._resource->transitionTo(cmdList, D3D12_RESOURCE_STATE_RENDER_TARGET);
                dsv._resource->transitionTo(cmdList, D3D12_RESOURCE_STATE_DEPTH_WRITE);
                cmdList->OMSetRenderTargets(1, &rtv._descriptorHandle->cpuHandle, FALSE,
                                            &dsv._descriptorHandle->cpuHandle);

                D3D12_VIEWPORT vp{0, 0, static_cast<float>(r->_width), static_cast<float>(r->_height), 0.f, 1.f};
                D3D12_RECT     sc{0, 0, static_cast<LONG>(r->_width), static_cast<LONG>(r->_height)};
                cmdList->RSSetViewports(1, &vp);
                cmdList->RSSetScissorRects(1, &sc);

                ID3D12DescriptorHeap* heaps[] = {
                    rM->_descriptorHeapAllocator_CBV_SRV_UAV.heap.Get()};
                cmdList->SetDescriptorHeaps(1, heaps);

                r->_psoManager->bindPipelineState(cmdList, toS(RN::pso_sky_pass));

                // Slot 0 : camera SRV
                auto camSRV = rM->getFrameView(
                    instanceM->pool<CameraInstance>()._instancePoolViewHandle)[frameIndex];
                camSRV._resource->transitionTo(cmdList, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
                cmdList->SetGraphicsRootDescriptorTable(0, camSRV._descriptorHandle->gpuHandle);

                // Slot 1 : 16 root constants — camIdx, skyHeapIdx, mode, horizonWidth, colorSky(4), colorHorizon(4), colorGround(4)
                EntityHandle cam;
                reg->view<Camera_C, Transform_C>().each(
                    [&](entt::entity e, Camera_C& c, Transform_C&)
                    { if (c._active) cam = {reg, e}; });
                if (!cam.valid())
                    return;
                auto camID = instanceM->pool<CameraInstance>().getGPUIndex(cam);
                if (!camID.valid())
                    return;

                uint32_t mode    = static_cast<uint32_t>(sky->mode_);
                uint32_t heapIdx = (tex) ? tex->heapIdx_ : 0xFFFFFFFFu;

                float constants[16];
                std::memcpy(&constants[0], &camID,   4);
                std::memcpy(&constants[1], &heapIdx, 4);
                std::memcpy(&constants[2], &mode,    4);
                constants[3]  = sky->horizonWidth_;
                constants[4]  = sky->color1_.x(); constants[5]  = sky->color1_.y();
                constants[6]  = sky->color1_.z(); constants[7]  = 0.0f;
                constants[8]  = sky->color2_.x(); constants[9]  = sky->color2_.y();
                constants[10] = sky->color2_.z(); constants[11] = 0.0f;
                constants[12] = sky->color3_.x(); constants[13] = sky->color3_.y();
                constants[14] = sky->color3_.z(); constants[15] = 0.0f;
                cmdList->SetGraphicsRoot32BitConstants(1, 16, constants, 0);

                // Slot 2 : full bindless heap (PS samples g_textures[skyHeapIdx])
                cmdList->SetGraphicsRootDescriptorTable(
                    2, rM->_descriptorHeapAllocator_CBV_SRV_UAV.heap
                           ->GetGPUDescriptorHandleForHeapStart());

                cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                cmdList->DrawInstanced(3, 1, 0, 0);
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
