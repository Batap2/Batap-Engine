#include "AssetFieldTypes.h"

#include "Assets/AssetHandle.h"
#include "Assets/AssetLoader.h"
#include "Assets/AssetManager.h"
#include "Assets/Material.h"
#include "Assets/Mesh.h"
#include "Assets/Texture.h"
#include "Engine.h"
#include "Reflection/ComponentRegistry.h"

#include <nlohmann/json.hpp>

#include <string>

namespace batap
{
namespace
{
template <class A>
void setAssetHandle(const char* typeName)
{
    auto& slot = fieldTypeSlot<AssetHandle<A>>();
    slot.typeName = typeName;

    slot.toJson = [](const void* f, nlohmann::json& out, const Engine& ctx)
    {
        const auto handle = *static_cast<const AssetHandle<A>*>(f);
        const std::string* path = handle ? ctx._assetManager->getPath<A>(handle) : nullptr;
        out = path ? nlohmann::json(*path) : nlohmann::json(nullptr);
    };

    slot.fromJson = [](void* f, const nlohmann::json& in, const Engine& ctx)
    {
        auto& handle = *static_cast<AssetHandle<A>*>(f);
        handle = {};
        if (!in.is_string())
            return;

        const std::string path = in.get<std::string>();
        auto found = ctx._assetManager->getHandle<A>(path);
        if (!found)
            if (auto any = loadAsset(path, ctx))
                if (auto* h = std::get_if<AssetHandle<A>>(&*any))
                    found = *h;
        if (found)
            handle = *found;
    };
}
}  // namespace

void registerAssetFieldTypes()
{
    setAssetHandle<Mesh>("MeshHandle");
    setAssetHandle<Texture>("TextureHandle");
    setAssetHandle<Material>("MaterialHandle");
}

}  // namespace batap
