#include "AssetFieldTypes.h"

#include "Assets/AssetHandle.h"
#include "Assets/AssetLoader.h"
#include "Assets/AssetManager.h"
#include "Shaders/ShaderInterop.h"
#include "Assets/Mesh.h"
#include "Assets/Texture.h"
#include "Engine.h"
#include "Reflection/ComponentRegistry.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <iostream>
#include <array>
#include <cstddef>
#include <string>

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

    const std::string path = in.get<std::string>();
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

    slot.toJson = [](const void* f, nlohmann::json& out, const Engine& ctx)
    { out = handleToJson<A>(*static_cast<const AssetHandle<A>*>(f), ctx); };

    slot.fromJson = [](void* f, const nlohmann::json& in, const Engine& ctx)
    { *static_cast<AssetHandle<A>*>(f) = handleFromJson<A>(in, ctx); };
}

// Fixed-size slot array (Materials_C). Always writes N entries, null for the
// empty ones, so a slot's index survives the round trip.
template <class A, std::size_t N>
void setAssetHandleArray(const char* typeName)
{
    using Arr = std::array<AssetHandle<A>, N>;
    auto& slot = fieldTypeSlot<Arr>();
    slot.typeName = typeName;

    slot.toJson = [](const void* f, nlohmann::json& out, const Engine& ctx)
    {
        const auto& arr = *static_cast<const Arr*>(f);
        out = nlohmann::json::array();
        for (const auto& h : arr)
            out.push_back(handleToJson<A>(h, ctx));
    };

    slot.fromJson = [](void* f, const nlohmann::json& in, const Engine& ctx)
    {
        auto& arr = *static_cast<Arr*>(f);
        arr = {};
        if (!in.is_array())
            return;
        const std::size_t n = std::min(N, in.size());
        for (std::size_t i = 0; i < n; ++i)
            arr[i] = handleFromJson<A>(in[i], ctx);
    };
}
}  // namespace

void registerAssetFieldTypes()
{
    setAssetHandle<Mesh>("MeshHandle");
    setAssetHandle<Texture>("TextureHandle");
    setAssetHandle<Material>("MaterialHandle");

    setAssetHandleArray<Material, 8>("MaterialHandle[8]");
}

}  // namespace batap
