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

void AssetManager::forEachAssetOfType(AssetType type, const ForEachAssetMetaFn& fn) const
{
    if (!fn)
        return;

    switch (type)
    {
        case AssetType::Mesh: {
            auto* map = std::get<AssetSlotMap<Mesh>*>(maps_);
            if (!map)
                return;

            map->slotMap_.for_each(
                [&](AssetSlotMap<Mesh>::InternalKey ik, const AssetSlotMap<Mesh>::Asset& a)
                {
                    fn(AssetHandleAny{AssetSlotMap<Mesh>::toPublic(ik)}, std::string_view(a.name_),
                       std::string_view(a.path_));
                });
            break;
        }
        case AssetType::Texture: {
            auto* map = std::get<AssetSlotMap<Texture>*>(maps_);
            if (!map)
                return;

            map->slotMap_.for_each(
                [&](AssetSlotMap<Texture>::InternalKey ik, const AssetSlotMap<Texture>::Asset& a)
                {
                    fn(AssetHandleAny{AssetSlotMap<Texture>::toPublic(ik)},
                       std::string_view(a.name_), std::string_view(a.path_));
                });
            break;
        }
    }
}

}  // namespace batap
