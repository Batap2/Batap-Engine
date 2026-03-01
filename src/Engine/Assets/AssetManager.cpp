#include "AssetManager.h"

#include "Mesh.h"
#include "Texture.h"

#include "AssetSlotMap.h"

namespace batap
{

AssetManager::AssetManager(ResourceManager* rm) : resourceManager_(rm)
{
    std::get<AssetSlotMap<Mesh>*>(maps_) = new AssetSlotMap<Mesh>();
    std::get<AssetSlotMap<Texture>*>(maps_) = new AssetSlotMap<Texture>();
}

AssetManager::~AssetManager()
{
    delete std::get<AssetSlotMap<Mesh>*>(maps_);
    delete std::get<AssetSlotMap<Texture>*>(maps_);
}

}  // namespace batap
