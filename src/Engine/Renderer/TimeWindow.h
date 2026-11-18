#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <span>
#include <vector>

namespace batap
{
struct TimeWindow
{
    void add(float ms) { ms_[count_++ % ms_.size()] = ms; }
    std::span<const float> recorded() const
    {
        return std::span(ms_).first(std::min(count_, ms_.size()));
    }
    float median() const
    {
        std::vector<float> times(recorded().begin(), recorded().end());
        if (times.empty())
            return 0.f;
        const auto middle = times.begin() + static_cast<std::ptrdiff_t>(times.size() / 2);
        std::nth_element(times.begin(), middle, times.end());
        return *middle;
    }

    static float ms(std::chrono::steady_clock::duration d)
    {
        return std::chrono::duration<float, std::milli>(d).count();
    }

   private:
    std::array<float, 256> ms_{};
    size_t count_ = 0;
};
}  // namespace batap
