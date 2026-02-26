#include "FileImporter.h"

#include "MeshImporter.h"

#include <unordered_set>

namespace batap
{

static std::string_view extractExtension(std::string_view path)
{
    const auto slash = path.find_last_of("/\\");
    const std::string_view filename = (slash == std::string_view::npos) ? path
                                                                        : path.substr(slash + 1);

    const auto dot = filename.find_last_of('.');
    if (dot == std::string_view::npos || dot + 1 >= filename.size())
        return {};

    return filename.substr(dot + 1);
}

std::vector<AssetHandleAny> importFile(std::string_view path, AssetManager& assetM)
{
    std::vector<AssetHandleAny> out;
    static constexpr std::array<std::string_view, 4> meshExtensions{"obj", "fbx", "gltf", "glb"};
    auto extension = extractExtension(path);

    if (std::find(meshExtensions.begin(), meshExtensions.end(), extension) != meshExtensions.end())
    {         
        auto meshH = importMeshFromFile(path, assetM);
        out.reserve(meshH.size());
        for(auto& handle : meshH){
            out.emplace_back(handle);
        }
    }

    return out;
}
}  // namespace batap
