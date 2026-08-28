#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace batap
{
// Runtime HLSL -> SPIR-V for hot reload; normal builds compile offline (dxc via CMake).
// libdxcompiler is dlopen'd on first compile(): if missing, hot reload is disabled
// but the engine still runs.
struct ShaderCompiler
{
    ~ShaderCompiler();

    std::vector<uint8_t> compile(const std::string& hlslPath, const char* target);

   private:
    bool ensureLoaded();

    void* library_ = nullptr;
    void* createInstance_ = nullptr;  // DxcCreateInstanceProc
    bool loadFailed_ = false;
};
}  // namespace batap
