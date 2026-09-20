#include "AssetManager.h"

#include "Shaders/ShaderInterop.h"
#include "Mesh.h"
#include "Texture.h"

#include "AssetGPUArena.h"
#include "AssetLoader.h"
#include "AssetSlotMap.h"
#include "Renderer/ResourceManager.h"
#include "Serialization/BmatSerializer.h"

#include <cassert>
#include <filesystem>

namespace batap
{

void AssetManager::setBaseDir(std::string dir)
{
    assert(!dir.empty() && "AssetManager::setBaseDir called with empty dir");
    baseDir_ = std::move(dir);
}

AssetManager::AssetManager(ResourceManager* rm) : resourceManager_(rm)
{
    std::get<AssetSlotMap<Mesh>*>(maps_)    = new AssetSlotMap<Mesh>();
    std::get<AssetSlotMap<Texture>*>(maps_) = new AssetSlotMap<Texture>();

    std::get<AssetGPUArena<Material>*>(gpuArenas_) = new AssetGPUArena<Material>(
        AssetGPUArena<Material>::create(*rm, 64, "MaterialArena"));
}

std::string AssetManager::texturePathOf(uint32_t bindlessIndex) const
{
    std::string result;
    getSlotMap<Texture>()->for_each(
        [&](TextureHandle, const AssetSlotMap<Texture>::Asset& a)
        {
            if (result.empty() && a.value_.bindlessIndex_ == bindlessIndex &&
                !isBuiltinAsset(a.path_))
                result = a.path_;
        });
    return result;
}

void AssetManager::saveAllAssets() const
{
    getGPUArena<Material>()->forEach(
        [&](AssetHandle<Material> key, const std::string& /*name*/, const std::string& relPath)
        {
            if (relPath.empty() || isBuiltinAsset(relPath)) return;
            const auto* mat = getGPUArena<Material>()->get(key);
            if (!mat) return;

            MaterialDesc desc;
            desc.mat = const_cast<Material*>(mat);
            desc.albedoTexPath = texturePathOf(mat->albedoTexIdx_);
            desc.normalTexPath = texturePathOf(mat->normalTexIdx_);
            desc.roughnessTexPath = texturePathOf(mat->roughnessTexIdx_);
            desc.metallicTexPath = texturePathOf(mat->metallicTexIdx_);

            const std::string absPath = (std::filesystem::path(baseDir_) / relPath).string();
            writeBmat(desc, absPath);
        });
}

template <>
bool AssetManager::unload<Mesh>(AssetHandle<Mesh> key)
{
    auto& map = *getSlotMap<Mesh>();
    Mesh* mesh = map.get(key);
    if (!mesh)
        return false;
    if (mesh->buffer_.valid())
        resourceManager_->requestDestroy(mesh->buffer_);
    return map.erase(key);
}

AssetManager::~AssetManager()
{
    delete std::get<AssetSlotMap<Mesh>*>(maps_);
    delete std::get<AssetSlotMap<Texture>*>(maps_);
    delete std::get<AssetGPUArena<Material>*>(gpuArenas_);
}
}  // namespace batap
