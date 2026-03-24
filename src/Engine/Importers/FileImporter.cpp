#include "FileImporter.h"
#include "MeshDecomposer.h"

#include <array>
#include <filesystem>
#include <string>

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

static std::string computeOutputDir(std::string_view filePath, const std::string& projectDir)
{
    namespace fs = std::filesystem;
    const auto fileDir = fs::path(filePath).parent_path();
    const auto rel = fs::relative(fileDir, fs::path(projectDir));
    const bool inside = !rel.empty() && rel.native()[0] != L'.';
    return inside ? fileDir.string() : projectDir;
}

ImportResult importFile(std::string_view path, ImportOptions opts)
{
    ImportResult out;

    if (opts.outputDir.empty())
    {
        out.kind = ImportResult::Kind::Failed;
        out.message = "No outputDir";
        return out;
    }

    static constexpr std::array<std::string_view, 4> k3dExtensions{"obj", "fbx", "gltf", "glb"};
    const auto ext = extractExtension(path);

    if (std::find(k3dExtensions.begin(), k3dExtensions.end(), ext) != k3dExtensions.end())
    {
        const std::string outDir = computeOutputDir(path, opts.outputDir);
        const auto decomp = decomposeSourceFile(path, outDir, opts.outputDir);

        if (decomp.ok)
        {
            out.kind = ImportResult::Kind::Decomposed;
            out.writtenFiles = decomp.bmeshPaths;
            out.writtenFiles.insert(out.writtenFiles.end(), decomp.texturePaths.begin(),
                                    decomp.texturePaths.end());
            out.writtenFiles.insert(out.writtenFiles.end(),
                                    decomp.btplPaths.begin(), decomp.btplPaths.end());
        }
        else
        {
            out.kind = ImportResult::Kind::Failed;
            out.message = "Decompose failed";
        }
    }

    return out;
}

}  // namespace batap
