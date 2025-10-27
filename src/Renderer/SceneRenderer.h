#pragma once

#include "DirectX-Headers/include/directx/d3d12.h"
#include "entt/entt.hpp"

#include "Scene.h"
#include "Renderer.h"
#include "Components/Camera_C.h"
#include "Components/Transform_C.h"

#include <cstdint>
#include <vector>

namespace rayvox
{
struct SceneRenderer
{
    SceneRenderer(Renderer* renderer) : _renderer(renderer)
    {
        initRenderPasses();
    }



    // ~SceneRenderer()
    // {
    //     // Détacher tous les callbacks
    //     _registry.on_construct<CameraComponent>().disconnect(this);
    //     _registry.on_construct<TransformComponent>().disconnect(this);

    //     _registry.on_destroy<CameraComponent>().disconnect(this);
    //     _registry.on_destroy<TransformComponent>().disconnect(this);
    // }

    // Setup des callbacks EnTT
    void setupCallbacks()
    {
        registerComponent<Camer>();
        registerComponent<Transform>();
    }

    // // Prépare la frame (collecte et upload les données dirty)
    // void prepareFrame(uint32_t frameIndex)
    // {
    //     uploadComponentType<CameraComponent>(frameIndex);
    //     uploadComponentType<TransformComponent>(frameIndex);
    // }

    // // Enregistrement des commandes de rendu (appelé par le Renderer)
    // void recordCommandsOpaque(ID3D12GraphicsCommandList* cmdList, uint32_t frameIndex)
    // {
    //     // Récupérer toutes les entités avec Mesh + Transform
    //     auto view = _registry.view<MeshComponent, TransformComponent>();

    //     for (auto entity : view)
    //     {
    //         auto& mesh = view.get<MeshComponent>(entity);
    //         auto& transform = view.get<TransformComponent>(entity);

    //         // Bind vertex/index buffers
    //         auto vbv = _renderer->getVertexBufferView(mesh.vertexBuffer_ID);
    //         auto ibv = _renderer->getIndexBufferView(mesh.indexBuffer_ID);
    //         cmdList->IASetVertexBuffers(0, 1, &vbv);
    //         cmdList->IASetIndexBuffer(&ibv);

    //         // Bind transform constant buffer
    //         cmdList->SetGraphicsRootConstantBufferView(
    //             0, _renderer->getGPUAddress(transform.buffer_ID));

    //         // Bind camera (on assume qu'il y a une seule caméra active)
    //         auto cameraView = _registry.view<CameraComponent>();
    //         if (!cameraView.empty())
    //         {
    //             auto& camera = cameraView.get<CameraComponent>(cameraView.front());
    //             cmdList->SetGraphicsRootConstantBufferView(
    //                 1, _renderer->getGPUAddress(camera.buffer_ID));
    //         }

    //         // Draw
    //         cmdList->DrawIndexedInstanced(mesh.indices.size(), 1, 0, 0, 0);
    //     }
    // }

    // void recordCommandsShadows(ID3D12GraphicsCommandList* cmdList, uint32_t frameIndex)
    // {
    //     auto lightView = _registry.view<LightComponent>();

    //     for (auto lightEntity : lightView)
    //     {
    //         auto& light = lightView.get<LightComponent>(lightEntity);

    //         if (!light.castsShadows)
    //             continue;

    //         // Render shadow map pour cette light
    //         // ... (similar à recordCommandsOpaque mais avec shadow PSO)
    //     }
    // }

    // void recordCommandsCompute(ID3D12GraphicsCommandList* cmdList, uint32_t frameIndex)
    // {
    //     // Exemple: particle system compute
    //     // auto view = _registry.view<ParticleSystemComponent>();
    //     // ...
    // }

    Scene* _scene = nullptr;
   private:
    Renderer* _renderer;

    void initRenderPasses();

    // Template pour enregistrer n'importe quel composant IGPUComponent
    // template <typename T>
    // void registerComponent()
    // {
    //     static_assert(std::is_base_of_v<IGPUComponent, T>,
    //                   "Component must inherit from IGPUComponent");

    //     _registry.on_construct<T>().template connect<&SceneRenderer::onComponentAdded<T>>(this);
    //     _registry.on_destroy<T>().template connect<&SceneRenderer::onComponentDestroyed<T>>(this);
    // }

    // template <typename T>
    // void onComponentAdded(entt::registry& reg, entt::entity entity)
    // {
    //     auto& component = reg.get<T>(entity);
    //     component.allocateGPUResources(_renderer);
    // }

    // template <typename T>
    // void onComponentDestroyed(entt::registry& reg, entt::entity entity)
    // {
    //     auto& component = reg.get<T>(entity);
    //     component.releaseGPUResources(_renderer);
    // }

    // template <typename T>
    // void uploadComponentType(uint32_t frameIndex)
    // {
    //     auto view = _registry.view<T>();
    //     for (auto entity : view)
    //     {
    //         auto& component = view.get<T>(entity);

    //         // Certains composants n'ont pas besoin d'upload chaque frame
    //         if (component.needsPerFrameUpload())
    //         {
    //             component.uploadIfDirty(_renderer, frameIndex);
    //         }
    //     }
    // }
};
}  // namespace rayvox