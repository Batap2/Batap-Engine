#pragma once

#include "DirectX-Headers/include/directx/d3d12.h"
#include "entt/entt.hpp"

#include "Components/Camera_C.h"
#include "Components/Transform_C.h"
#include "Renderer.h"
#include "Scene.h"


#include <cstdint>
#include <vector>

namespace rayvox
{
struct SceneRenderer
{
    SceneRenderer(Renderer* renderer) : _renderer(renderer)
    {
    }

    void initRenderPasses();

    void loadScene(Scene* scene);
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
        registerComponent<Camera_C>();
        registerComponent<Transform_C>();
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

    Scene* _scene = nullptr;

   private:
    Renderer* _renderer;

    // Template pour enregistrer n'importe quel composant IGPUComponent
    template <typename T>
    void registerComponent()
    {
        _scene->_registry.on_construct<T>().template connect<&SceneRenderer::onComponentAdded<T>>(
            this);
        //_scene->_registry.on_destroy<T>().template
        //connect<&SceneRenderer::onComponentDestroyed<T>>(this);
    }

    // soit faire une fonction ça par component, y compris onComponentDestroyed et uploadIfDirty
    template <typename T>
    void onComponentAdded(entt::registry& reg, entt::entity entity)
    {
        auto& component = reg.get<T>(entity);
    }

    // template<typename T>
    // void uploadDirty(uint32_t frameIndex) {
    //     const uint32_t bit = 1u << frameIndex;
    //     auto view = _reg.view<T>();
    //     for (auto e : view) {
    //         auto& c = view.get<T>(e);
    //         if (c.dirtyMask & bit) {
    //             ComponentTraits<T>::upload(_renderer, c.buffer_ID, c, frameIndex);
    //             c.dirtyMask &= ~bit;
    //         }
    //     }
    // }
};
}  // namespace rayvox