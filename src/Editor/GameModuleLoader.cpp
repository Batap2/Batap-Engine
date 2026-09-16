#include "GameModuleLoader.h"

#include "Reflection/ComponentRegistry.h"
#include "UI/FieldUI.h"

#include "imgui.h"

#include <iostream>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace batap
{
namespace fs = std::filesystem;

bool GameModuleLoader::load(const std::string& dllPath)
{
    sourcePath_ = dllPath;
    return stage() && loadStaged();
}

// Windows locks loaded modules: a copy is loaded so the linker can keep
// overwriting the real one. Staging is separate from loading so a locked
// file (linker mid-write) just means "retry next frame".
bool GameModuleLoader::stage()
{
    std::error_code ec;
    const auto mtime = fs::last_write_time(sourcePath_, ec);
    if (ec)
        return false;

    fs::path staged = sourcePath_;
    staged.replace_filename(sourcePath_.stem().string() + "_loaded_" +
                            std::to_string(generation_++) + ".dll");
    fs::copy_file(sourcePath_, staged, fs::copy_options::overwrite_existing, ec);
    if (ec)
        return false;

    stagedPath_ = staged;
    loadedMtime_ = mtime;
    return true;
}

bool GameModuleLoader::loadStaged()
{
#if defined(_WIN32)
    lib_ = ::LoadLibraryA(stagedPath_.string().c_str());
#else
    lib_ = ::dlopen(sourcePath_.string().c_str(), RTLD_NOW);
#endif
    if (!lib_)
    {
        std::cerr << "[GameModule] failed to load " << stagedPath_.string() << "\n";
        return false;
    }
    loadedPath_ = stagedPath_;

#if defined(_WIN32)
    // GetProcAddress returns a generic function pointer by design.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-function-type-strict"
    auto entry = reinterpret_cast<GameModuleEntryFn>(
        ::GetProcAddress(static_cast<HMODULE>(lib_), GameModuleEntryName));
#pragma clang diagnostic pop
#else
    auto entry = reinterpret_cast<GameModuleEntryFn>(::dlsym(lib_, GameModuleEntryName));
#endif
    if (!entry)
    {
        std::cerr << "[GameModule] " << GameModuleEntryName << " not found in "
                  << sourcePath_.string() << "\n";
        return false;
    }

    api_ = {};
    api_.imguiContext_ = ImGui::GetCurrentContext();
    ImGui::GetAllocatorFunctions(&api_.imguiAlloc_, &api_.imguiFree_, &api_.imguiUserData_);
    entry(&api_);
    ComponentRegistry::instance().importFrom(*api_.registry_);

    // Imported fields point at the DLL's field type slots, which have no
    // editor half.
    for (const ComponentType& t : ComponentRegistry::instance().all())
        for (const Field& f : t.fields)
            if (!f.type->drawUI)
                installFieldUIFor(*f.type);

    ComponentRegistry::instance().validate();
    std::cerr << "[GameModule] loaded " << loadedPath_.string() << "\n";
    return true;
}

bool GameModuleLoader::stagePending()
{
    if (!loaded())
        return false;

    const auto now = std::chrono::steady_clock::now();
    if (now - lastCheck_ < std::chrono::milliseconds(500))
        return false;
    lastCheck_ = now;

    std::error_code ec;
    const auto mtime = fs::last_write_time(sourcePath_, ec);
    if (ec || mtime == loadedMtime_)
        return false;

    return stage();
}

bool GameModuleLoader::swapStaged()
{
    const fs::path old = loadedPath_;
    unload();

    std::error_code ec;
    fs::remove(old, ec);

    return loadStaged();
}

void GameModuleLoader::unload()
{
    if (!lib_)
        return;
#if defined(_WIN32)
    ::FreeLibrary(static_cast<HMODULE>(lib_));
#else
    ::dlclose(lib_);
#endif
    lib_ = nullptr;
    api_ = {};
}

std::unique_ptr<Game> GameModuleLoader::makeGame() const
{
    return api_.createGame_ ? std::unique_ptr<Game>(api_.createGame_()) : nullptr;
}

GameModuleLoader::~GameModuleLoader()
{
    unload();
}

}  // namespace batap
