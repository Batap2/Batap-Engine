#include "Paths.h"

#include <filesystem>

#include "Platform/PlatformWindow.h"

namespace batap
{
std::string resolveEngineFile(const char* shippedRelative, const char* sourceRelative)
{
    namespace fs = std::filesystem;

    const fs::path shipped = fs::path(platformExeDir()) / shippedRelative;
    if (fs::exists(shipped))
        return shipped.string();

    return (fs::path(BATAP_ROOT_DIR) / sourceRelative).string();
}
}  // namespace batap
