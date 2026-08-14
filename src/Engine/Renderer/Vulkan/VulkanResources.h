#pragma once

#include <volk.h>

#include "Handles.h"
#include "Renderer/EngineConfig.h"
#include "Renderer/ResourceFormat.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

VK_DEFINE_HANDLE(VmaAllocator)
VK_DEFINE_HANDLE(VmaAllocation)

namespace batap
{
struct VulkanContext;

// Backend Vulkan du ResourceManager — même nom de classe que la version DX12 :
// le header Renderer/ResourceManager.h sélectionne l'un ou l'autre par
// plateforme, les appelants (AssetLoader, InstanceManager, GPUArena…) ne
// changent pas. Internes repensés pour Vulkan :
// - pas d'objets « view » pour les buffers : les handles de view/mesh view ne
//   sont que des alias + métadonnées de bind ({buffer, offset, stride}) ;
// - un seul set bindless global pour les textures (sampler immutable +
//   tableau variable partially-bound/update-after-bind), indexé par le même
//   bindlessIndex que les shaders utilisent via heapIdx ;
// - staging ring par frame, mappé en permanence : requestUploadOwned rend un
//   span directement dans le ring (zéro copie), flushUploads enregistre les
//   copies + barrières ;
// - frame resources : FramesInFlight buffers derrière un seul handle, la copie
//   est routée vers le buffer de la frame au flush ;
// - destruction différée par slot de frame.
struct ResourceManager
{
    struct StructuredBuffer
    {
        GPUResourceHandle resource;
        GPUViewHandle srv;
    };

    struct Texture2D
    {
        GPUResourceHandle resource;
        GPUViewHandle srv;
        uint32_t bindlessIndex = 0;
    };

    // Métadonnées de bind d'un vertex/index buffer (pas d'objet GPU en Vulkan)
    struct MeshView
    {
        GPUResourceHandle resource;
        uint64_t offset = 0;
        uint64_t size = 0;
        uint32_t stride = 0;                              // VBV
        VkIndexType indexType = VK_INDEX_TYPE_UINT32;     // IBV
        bool isIndex = false;
    };

    void init(VulkanContext& ctx, uint32_t framesInFlight,
              uint64_t stagingBytesPerFrame = StagingBytesPerFrame);
    void shutdown();

    // ---- API neutre (contrat commun avec le backend DX12) ----
    GPUResourceHandle createStaticBuffer(uint64_t sizeBytes,
                                         std::optional<std::string_view> name = std::nullopt);

    StructuredBuffer
    createStaticStructuredBuffer(uint64_t elementCount, uint32_t elementStride,
                                 std::optional<std::string_view> name = std::nullopt);

    StructuredBuffer
    createFrameStructuredBuffer(uint64_t elementCount, uint32_t elementStride,
                                std::optional<std::string_view> name = std::nullopt);

    Texture2D createTexture2D(uint32_t width, uint32_t height, ResourceFormat format,
                              std::optional<std::string_view> name = std::nullopt);

    GPUMeshViewHandle createStaticIBV(GPUResourceHandle resource,
                                      ResourceFormat format = ResourceFormat::R32_UINT,
                                      std::optional<std::string_view> name = std::nullopt,
                                      uint64_t offset = 0, uint64_t size = 0);

    GPUMeshViewHandle createStaticVBV(GPUResourceHandle resource, uint32_t strideBytes,
                                      std::optional<std::string_view> name = std::nullopt,
                                      uint64_t offset = 0, uint64_t size = 0);

    uint32_t bindlessIndex(GPUViewHandle view);

    // Rend un span de dataSize octets dans le staging ring de la frame
    // courante. Si subRegionSize != 0, seule cette sous-région (à partir de
    // subRegionOffset dans le span) est copiée vers destinationOffset.
    std::span<std::byte> requestUploadOwned(GPUResourceHandle dest, uint64_t dataSize,
                                            uint32_t alignment, uint64_t destinationOffset = 0,
                                            uint64_t subRegionOffset = 0,
                                            uint64_t subRegionSize = 0);
    std::span<std::byte> requestUploadOwned(GPUViewHandle dest, uint64_t dataSize,
                                            uint32_t alignment, uint64_t destinationOffset = 0,
                                            uint64_t subRegionOffset = 0,
                                            uint64_t subRegionSize = 0);

    std::span<std::byte> requestTextureUploadOwned(GPUResourceHandle dest, uint32_t width,
                                                   uint32_t height, ResourceFormat format,
                                                   uint32_t& outRowPitch);

    void requestDestroy(GPUResourceHandle handle);
    void requestDestroy(GPUViewHandle handle, bool destroyAssociatedResources = false);

    // ---- Cycle de frame (appelé par le Renderer) ----
    void flushUploads(VkCommandBuffer cmd, uint32_t frameIndex);
    void beginFrame(uint32_t frameIndex);

    // ---- Internes exposés au backend (Renderer Vulkan, pas le moteur) ----
    struct Buffer
    {
        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation allocation = nullptr;
        uint64_t size = 0;
    };

    struct Image
    {
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation allocation = nullptr;
        VkImageView view = VK_NULL_HANDLE;
        VkFormat format = VK_FORMAT_UNDEFINED;
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t bindlessIndex = 0;
    };

    Buffer* getBuffer(GPUResourceHandle handle);                       // static
    Buffer* getFrameBuffer(GPUResourceHandle handle, uint32_t frame);  // frame
    Image* getImage(GPUResourceHandle handle);
    MeshView* getMeshView(GPUMeshViewHandle handle);

    // Le VkBuffer derrière une view (SRV de structured buffer), résolu pour
    // la frame donnée — c'est ce que les passes bindent dans leur set.
    VkBuffer bufferForView(GPUViewHandle view, uint32_t frame);

    VkDevice device() const;

    // Pour construire les pipeline layouts des passes
    VkDescriptorSetLayout bindlessLayout_ = VK_NULL_HANDLE;
    VkDescriptorSet bindlessSet_ = VK_NULL_HANDLE;

   private:
    struct StagingRing
    {
        Buffer buffer;
        std::byte* mapped = nullptr;
        uint64_t offset = 0;
    };

    struct UploadRequest
    {
        GPUResourceHandle dest;
        uint64_t srcOffset = 0;
        uint64_t size = 0;
        uint64_t dstOffset = 0;
        // texture uniquement
        bool isTexture = false;
        uint32_t texWidth = 0;
        uint32_t texHeight = 0;
    };

    Buffer createBufferInternal(uint64_t sizeBytes);
    std::byte* stagingAlloc(uint64_t size, uint64_t alignment, uint64_t& outOffset);
    uint32_t allocBindlessIndex();
    void destroyNow(Buffer& b);
    void destroyNow(Image& i);

    VulkanContext* ctx_ = nullptr;
    VmaAllocator allocator_ = nullptr;
    uint32_t framesInFlight_ = 0;
    uint32_t currentFrame_ = 0;

    std::unordered_map<GPUResourceHandle, Buffer> buffers_;
    std::unordered_map<GPUResourceHandle, std::vector<Buffer>> frameBuffers_;
    std::unordered_map<GPUResourceHandle, Image> images_;
    std::unordered_map<GPUViewHandle, GPUResourceHandle> viewToResource_;
    std::unordered_map<GPUMeshViewHandle, MeshView> meshViews_;

    std::vector<StagingRing> staging_;
    std::vector<UploadRequest> uploadRequests_;

    struct DestroyQueue
    {
        std::vector<Buffer> buffers;
        std::vector<Image> images;
    };
    std::vector<DestroyQueue> destroyQueues_;

    VkSampler defaultSampler_ = VK_NULL_HANDLE;
    VkDescriptorPool bindlessPool_ = VK_NULL_HANDLE;
    uint32_t bindlessCapacity_ = 0;
    uint32_t bindlessNext_ = 0;
    std::vector<uint32_t> bindlessFree_;
};
}  // namespace batap
