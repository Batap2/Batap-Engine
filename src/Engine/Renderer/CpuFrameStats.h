#pragma once

#include <array>
#include <cstddef>

namespace batap
{
struct CpuFrameStats
{
    struct Frame
    {
        float total_ = 0.f;
        float fence_ = 0.f;
        float acquire_ = 0.f;
        float record_ = 0.f;
        float present_ = 0.f;
    };

    void add(const Frame& frame) { frames_[count_++ % frames_.size()] = frame; }
    void print() const;

   private:
    std::array<Frame, 1024> frames_{};
    size_t count_ = 0;
};
}  // namespace batap
