#include "VulkanShaderCompiler.h"

#if defined(_WIN32)
  #define WIN32_LEAN_AND_MEAN
  #define NOMINMAX
  #include <windows.h>  // avant dxcapi.h (HRESULT, IUnknown)
#else
  #include <dlfcn.h>
#endif

#if defined(__clang__)
  #pragma clang diagnostic push
  #pragma clang diagnostic ignored "-Weverything"
#endif
#include <dxc/dxcapi.h>
#if defined(__clang__)
  #pragma clang diagnostic pop
#endif

#include <fstream>
#include <iostream>

namespace batap
{

namespace
{
std::wstring widen(const std::string& s)
{
    return {s.begin(), s.end()};  // les chemins/targets sont ASCII
}

// RAII COM minimal, portable Windows/mac (CComPtr n'existe que dans le
// WinAdapter non-Windows du SDK, et ATL n'est pas une dépendance du moteur)
template <typename T>
struct ComPtr
{
    T* p = nullptr;
    ~ComPtr()
    {
        if (p)
            p->Release();
    }
    ComPtr() = default;
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    T** operator&() { return &p; }
    T* operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
};

// Le nom natif de la lib DXC, et son chargement dynamique par OS
#if defined(_WIN32)
void* loadDxcLibrary()
{
    // 1. à côté de l'exe (copiée par CMake) ou dans le PATH
    if (HMODULE lib = ::LoadLibraryA("dxcompiler.dll"))
        return lib;
    // 2. le SDK Vulkan repéré au configure
  #if defined(BATAP_DXC_LIB_DIR)
    return ::LoadLibraryA(BATAP_DXC_LIB_DIR "/dxcompiler.dll");
  #else
    return nullptr;
  #endif
}
void* loadDxcSymbol(void* lib)
{
    return reinterpret_cast<void*>(
        ::GetProcAddress(static_cast<HMODULE>(lib), "DxcCreateInstance"));
}
void closeDxcLibrary(void* lib)
{
    ::FreeLibrary(static_cast<HMODULE>(lib));
}
#else
void* loadDxcLibrary()
{
    // 1. les chemins dlopen standards (DYLD_LIBRARY_PATH couvre le cas brew)
    if (void* lib = dlopen("libdxcompiler.dylib", RTLD_NOW | RTLD_LOCAL))
        return lib;
    // 2. le SDK LunarG repéré au configure (le même qui fournit dxc au build)
  #if defined(BATAP_DXC_LIB_DIR)
    return dlopen(BATAP_DXC_LIB_DIR "/libdxcompiler.dylib", RTLD_NOW | RTLD_LOCAL);
  #else
    return nullptr;
  #endif
}
void* loadDxcSymbol(void* lib)
{
    return dlsym(lib, "DxcCreateInstance");
}
void closeDxcLibrary(void* lib)
{
    dlclose(lib);
}
#endif
}  // namespace

ShaderCompiler::~ShaderCompiler()
{
    if (library_)
        closeDxcLibrary(library_);
}

bool ShaderCompiler::ensureLoaded()
{
    if (createInstance_)
        return true;
    if (loadFailed_)
        return false;

    library_ = loadDxcLibrary();
    if (library_)
        createInstance_ = loadDxcSymbol(library_);

    if (!createInstance_)
    {
        std::cerr << "[ShaderCompiler] dxcompiler introuvable — hot reload désactivé\n";
        loadFailed_ = true;
        return false;
    }
    return true;
}

std::vector<uint8_t> ShaderCompiler::compile(const std::string& hlslPath, const char* target)
{
    if (!ensureLoaded())
        return {};

    std::ifstream file(hlslPath, std::ios::binary);
    if (!file)
    {
        std::cerr << "[ShaderCompiler] source introuvable : " << hlslPath << "\n";
        return {};
    }
    const std::string source{std::istreambuf_iterator<char>(file),
                             std::istreambuf_iterator<char>()};

    auto create = reinterpret_cast<DxcCreateInstanceProc>(createInstance_);

    ComPtr<IDxcCompiler3> compiler;
    if (FAILED(create(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler))))
    {
        std::cerr << "[ShaderCompiler] DxcCreateInstance a échoué\n";
        return {};
    }

    ComPtr<IDxcUtils> utils;
    ComPtr<IDxcIncludeHandler> includeHandler;
    if (SUCCEEDED(create(CLSID_DxcUtils, IID_PPV_ARGS(&utils))))
        utils->CreateDefaultIncludeHandler(&includeHandler);

    const std::wstring wPath = widen(hlslPath);
    const std::wstring wTarget = widen(target);

    // Les mêmes flags que la compilation hors-ligne (CMake)
    LPCWSTR args[] = {
        wPath.c_str(),
        L"-E", L"main",
        L"-T", wTarget.c_str(),
        L"-spirv",
        L"-fspv-target-env=vulkan1.3",
    };

    DxcBuffer buffer{};
    buffer.Ptr = source.data();
    buffer.Size = source.size();
    buffer.Encoding = DXC_CP_UTF8;

    ComPtr<IDxcResult> result;
    if (FAILED(compiler->Compile(&buffer, args, static_cast<UINT32>(std::size(args)),
                                 includeHandler.p, IID_PPV_ARGS(&result))))
    {
        std::cerr << "[ShaderCompiler] Compile() a échoué : " << hlslPath << "\n";
        return {};
    }

    // Erreurs et warnings, même quand la compilation réussit
    ComPtr<IDxcBlobUtf8> errors;
    result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
    if (errors && errors->GetStringLength() > 0)
        std::cerr << errors->GetStringPointer();

    HRESULT status = S_OK;
    result->GetStatus(&status);
    if (FAILED(status))
        return {};

    ComPtr<IDxcBlob> object;
    if (FAILED(result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&object), nullptr)) || !object)
        return {};

    const auto* data = static_cast<const uint8_t*>(object->GetBufferPointer());
    return {data, data + object->GetBufferSize()};
}

}  // namespace batap
