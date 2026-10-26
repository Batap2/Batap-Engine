#include "Renderer/Device/ShaderLibrary.h"

#include "Paths.h"
#include "Renderer/Device/VulkanContext.h"

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

namespace batap
{
ShaderLibrary::ShaderLibrary(VulkanContext& ctx) : ctx_(ctx)
{
    const std::string shaderDir = resolveEngineFile("shaders", "shaders");
    for (size_t i = 0; i < ShaderCount; ++i)
    {
        if (kShaders[i].rayTracing_ && !ctx_.rtSupported_)
            continue;
        owned_[i].emplace(ctx_.device_, shaderDir + "/" + kShaders[i].name_ + ".spv");
        modules_[i] = *owned_[i];
    }
}

bool ShaderLibrary::reloadIfChanged()
{
    namespace fs = std::filesystem;

    const fs::path sourceDir = fs::path(BATAP_ROOT_DIR) / "src/Engine/Shaders";

    fs::file_time_type latest{};
    std::error_code ec;
    for (const fs::directory_entry& entry : fs::directory_iterator(sourceDir, ec))
        if (entry.is_regular_file(ec))
            latest = std::max(latest, entry.last_write_time(ec));

    if (sourcesMtime_ == fs::file_time_type{})
    {
        sourcesMtime_ = latest;
        return false;
    }
    if (latest <= sourcesMtime_)
        return false;
    sourcesMtime_ = latest;

    std::array<std::vector<uint8_t>, ShaderCount> spirv;
    for (size_t i = 0; i < ShaderCount; ++i)
    {
        if (kShaders[i].rayTracing_ && !ctx_.rtSupported_)
            continue;
        const std::string source =
            (sourceDir / (std::string(kShaders[i].source_) + ".hlsl")).string();
        spirv[i] = ctx_.shaderCompiler_.compile(
            source, kShaders[i].target_, kShaders[i].rayTracing_ ? kRayTracingDefine : nullptr);
        if (spirv[i].empty())
        {
            std::cerr << "[ShaderCompiler] " << kShaders[i].name_
                      << " : compilation echouee, pipelines conservees\n";
            return false;
        }
    }

    vkDeviceWaitIdle(ctx_.device_);
    for (size_t i = 0; i < ShaderCount; ++i)
    {
        if (spirv[i].empty())
            continue;
        owned_[i].emplace(ctx_.device_, spirv[i].data(), spirv[i].size());
        modules_[i] = *owned_[i];
    }
    std::cout << "[ShaderCompiler] shaders reloaded" << std::endl;
    return true;
}
}  // namespace batap
