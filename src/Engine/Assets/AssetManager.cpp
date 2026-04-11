#include "AssetManager.h"

#include "Material.h"
#include "Mesh.h"
#include "Texture.h"

#include "AssetGPUArena.h"
#include "AssetSlotMap.h"
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

void AssetManager::saveAllAssets() const
{
    getGPUArena<Material>()->forEach(
        [&](AssetHandle<Material> key, const std::string& /*name*/, const std::string& relPath)
        {
            if (relPath.empty()) return;
            const auto* mat = getGPUArena<Material>()->get(key);
            if (!mat) return;
            const std::string absPath = (std::filesystem::path(baseDir_) / relPath).string();
            writeBmat(*mat, absPath);
        });
}

AssetManager::~AssetManager()
{
    delete std::get<AssetSlotMap<Mesh>*>(maps_);
    delete std::get<AssetSlotMap<Texture>*>(maps_);
    delete std::get<AssetGPUArena<Material>*>(gpuArenas_);
}
}  // namespace batap
