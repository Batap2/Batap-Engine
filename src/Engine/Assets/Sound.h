#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace batap
{
struct Sound
{
    std::filesystem::path file_;
    std::vector<float> samples_;
    uint32_t channels_ = 0;
    uint32_t sampleRate_ = 0;
    uint64_t frames_ = 0;

    bool streamed() const { return samples_.empty(); }
    float seconds() const
    {
        return sampleRate_ ? static_cast<float>(frames_) / static_cast<float>(sampleRate_) : 0.f;
    }
};
}  // namespace batap
