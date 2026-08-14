#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace batap
{
// HLSL -> SPIR-V à l'exécution, via libdxcompiler.dylib — la même API
// IDxcCompiler3 que le chemin DXIL Windows, un flag -spirv en plus.
// Sert au hot reload ; le build normal compile hors-ligne (dxc via CMake).
//
// La lib est chargée en dlopen au premier compile() : son absence n'empêche
// pas le moteur de tourner, elle désactive juste le hot reload.
struct ShaderCompiler
{
    ~ShaderCompiler();

    // target : "vs_6_6" / "ps_6_6" / ... ; entry point "main".
    // Rend un bytecode vide en cas d'échec (lib absente, erreur HLSL) —
    // le message d'erreur DXC part sur stderr, l'appelant garde ses
    // pipelines courantes.
    std::vector<uint8_t> compile(const std::string& hlslPath, const char* target);

   private:
    bool ensureLoaded();

    void* library_ = nullptr;
    void* createInstance_ = nullptr;  // DxcCreateInstanceProc
    bool loadFailed_ = false;
};
}  // namespace batap
