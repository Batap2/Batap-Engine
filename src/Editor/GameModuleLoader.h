#pragma once

#include "GameModule.h"

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>

namespace batap
{
struct GameModuleLoader
{
    ~GameModuleLoader();

    bool load(const std::string& dllPath);
    std::unique_ptr<Game> makeGame() const;
    bool loaded() const { return lib_ != nullptr; }

    // Why the last load failed.
    const std::string& lastError() const { return lastError_; }

    bool stagePending();
    bool swapStaged();

    GameModuleAPI api_{};
    void* lib_ = nullptr;

   private:
    bool stage();
    bool loadStaged();
    void unload();

    std::string lastError_;
    std::filesystem::path sourcePath_;
    std::filesystem::path loadedPath_;
    std::filesystem::path stagedPath_;
    std::filesystem::file_time_type loadedMtime_{};
    std::chrono::steady_clock::time_point lastCheck_{};
    uint32_t generation_ = 0;
};
}  // namespace batap
