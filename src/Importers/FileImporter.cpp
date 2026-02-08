#include "FileImporter.h"

#include "MeshImporter.h"

#include <unordered_set>

namespace rayvox
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

std::vector<AssetHandle> importFile(std::string_view path, AssetManager& assetM)
{
    static constexpr std::array<std::string_view, 4> meshExtensions{"obj", "fbx", "gltf", "glb"};
    auto extension = extractExtension(path);

    if (std::find(meshExtensions.begin(), meshExtensions.end(), extension) != meshExtensions.end())
    {
        return importMeshFromFile(path, assetM);
    }

    return {};
}
}  // namespace rayvox
