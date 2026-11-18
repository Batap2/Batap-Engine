#include "Renderer/CpuFrameStats.h"

#include <algorithm>
#include <iostream>
#include <span>
#include <utility>
#include <vector>

namespace batap
{
namespace
{
using Part = float (*)(const CpuFrameStats::Frame&);

constexpr std::array<std::pair<const char*, Part>, 5> kParts{{
    {"attente du GPU", [](const CpuFrameStats::Frame& f) { return f.fence_; }},
    {"acquire", [](const CpuFrameStats::Frame& f) { return f.acquire_; }},
    {"graphe et envoi", [](const CpuFrameStats::Frame& f) { return f.record_; }},
    {"présentation", [](const CpuFrameStats::Frame& f) { return f.present_; }},
    {"jeu et reste", [](const CpuFrameStats::Frame& f)
     { return f.total_ - f.fence_ - f.acquire_ - f.record_ - f.present_; }},
}};

float median(std::vector<float> values)
{
    const auto middle = values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2);
    std::nth_element(values.begin(), middle, values.end());
    return *middle;
}
}  // namespace

void CpuFrameStats::print() const
{
    const std::span<const Frame> frames =
        std::span(frames_).first(std::min(count_, frames_.size()));
    if (frames.empty())
        return;
    std::vector<float> totals;
    totals.reserve(frames.size());
    float sum = 0.f;
    for (const Frame& frame : frames)
    {
        totals.push_back(frame.total_);
        sum += frame.total_;
    }
    std::ranges::sort(totals);
    const auto at = [&](size_t percent)
    { return totals[std::min(totals.size() - 1, totals.size() * percent / 100)]; };
    const float slow = at(95);

    std::cout << "[Vulkan] frame CPU : médiane " << at(50) << " ms, moyenne "
              << sum / static_cast<float>(frames.size()) << ", p99 " << at(99) << ", max "
              << totals.back() << " ms sur " << frames.size() << " frames\n  médianes :";
    for (const auto& [name, part] : kParts)
    {
        std::vector<float> values;
        values.reserve(frames.size());
        for (const Frame& frame : frames)
            values.push_back(part(frame));
        std::cout << " " << name << " " << median(std::move(values));
    }
    std::cout << " ms\n  frames de " << slow << " ms ou plus, moyennes :";
    for (const auto& [name, part] : kParts)
    {
        float total = 0.f;
        size_t count = 0;
        for (const Frame& frame : frames)
            if (frame.total_ >= slow)
            {
                total += part(frame);
                ++count;
            }
        std::cout << " " << name << " " << total / static_cast<float>(count);
    }
    std::cout << " ms" << std::endl;
}
}  // namespace batap
