#pragma once

#include <string>

namespace batap
{
// Engine files (shaders, fonts, ...) live next to the executable in a
// shipped build, and in the source tree (BATAP_ROOT_DIR) during development.
// Never resolved from the working directory: it depends on how the app was
// launched.
std::string resolveEngineFile(const char* shippedRelative, const char* sourceRelative);
}  // namespace batap
