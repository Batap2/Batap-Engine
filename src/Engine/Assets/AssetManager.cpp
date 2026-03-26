#include "AssetManager.h"

#include "Material.h"
#include "Mesh.h"
#include "Texture.h"

#include "AssetGPUArena.h"
#include "AssetSlotMap.h"

#include <cassert>

namespace batap
{

void AssetManager::setBaseDir(std::string dir)
{
    assert(!dir.empty() && "AssetManager::setBaseDir called with empty dir");
    baseDir_ = std::move(dir);
}

AssetManager::AssetManager(ResourceManager* rm) : resourceManager_(rm)
{
    std::get<AssetSlotMap<Mesh>*>(maps_) = new AssetSlotMap<Mesh>();
    std::get<AssetSlotMap<Texture>*>(maps_) = new AssetSlotMap<Texture>();

    std::get<AssetGPUArena<Material>*>(gpuArenas_) = new AssetGPUArena<Material>(
        AssetGPUArena<Material>::create(*rm, 64, "MaterialArena"));
}

AssetManager::~AssetManager()
{
    delete std::get<AssetSlotMap<Mesh>*>(maps_);
    delete std::get<AssetSlotMap<Texture>*>(maps_);
    delete std::get<AssetGPUArena<Material>*>(gpuArenas_);
}
}  // namespace batap
