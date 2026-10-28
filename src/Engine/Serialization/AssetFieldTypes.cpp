#include "AssetFieldTypes.h"

#include "Assets/AssetHandle.h"
#include "Assets/AssetLoader.h"
#include "Assets/AssetManager.h"
#include "Assets/Mesh.h"
#include "Assets/Texture.h"
#include "Engine.h"
#include "Reflection/ComponentRegistry.h"
#include "Shaders/ShaderInterop.h"

#include <nlohmann/json.hpp>

#include <iostream>
#include <string>
#include <vector>

namespace batap
{
namespace
{
// A handle is meaningless across runs — index/generation depend on load
// order — so the path is what goes on disk, and reloading resolves it back.
template <class A>
nlohmann::json handleToJson(AssetHandle<A> handle, const Engine& ctx)
{
    const std::string* path = handle ? ctx.assetManager_->getPath<A>(handle) : nullptr;
    return path ? nlohmann::json(*path) : nlohmann::json(nullptr);
}

template <class A>
AssetHandle<A> handleFromJson(const nlohmann::json& in, const Engine& ctx)
{
    if (!in.is_string())
        return {};

    const std::string path = normalizeAssetPath(in.get<std::string>());
    auto found = ctx.assetManager_->getHandle<A>(path);
    if (!found)
        if (auto any = loadAsset(path, ctx))
            if (auto* h = std::get_if<AssetHandle<A>>(&*any))
                found = *h;
    if (!found)
        std::cerr << "[AssetFieldTypes] handle non résolu : " << path << "\n";
    return found ? *found : AssetHandle<A>{};
}

template <class A>
void setAssetHandle(const char* typeName)
{
    auto& slot = fieldTypeSlot<AssetHandle<A>>();
    slot.typeName = typeName;

    slot.toJson = [](const void* f, nlohmann::json& out, const FieldIO& io)
    { out = handleToJson<A>(*static_cast<const AssetHandle<A>*>(f), io.engine_); };

    slot.fromJson = [](void* f, const nlohmann::json& in, const FieldIO& io)
    { *static_cast<AssetHandle<A>*>(f) = handleFromJson<A>(in, io.engine_); };
}

template <class A>
void setAssetHandleList(const char* typeName)
{
    using List = std::vector<AssetHandle<A>>;
    auto& slot = fieldTypeSlot<List>();
    slot.typeName = typeName;

    slot.toJson = [](const void* f, nlohmann::json& out, const FieldIO& io)
    {
        out = nlohmann::json::array();
        for (const auto& h : *static_cast<const List*>(f))
            out.push_back(handleToJson<A>(h, io.engine_));
    };

    slot.fromJson = [](void* f, const nlohmann::json& in, const FieldIO& io)
    {
        auto& list = *static_cast<List*>(f);
        list.clear();
        if (!in.is_array())
            return;
        for (const auto& j : in)
            list.push_back(handleFromJson<A>(j, io.engine_));
        while (!list.empty() && !list.back())
            list.pop_back();
    };
}
}  // namespace

void registerAssetFieldTypes()
{
    setAssetHandle<Mesh>("MeshHandle");
    setAssetHandle<Texture>("TextureHandle");
    setAssetHandle<Material>("MaterialHandle");

    setAssetHandleList<Material>("MaterialHandle[]");
}

}  // namespace batap
