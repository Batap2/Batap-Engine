#include "Renderer/Graph/PassTimer.h"

#include "Platform/PlatformWindow.h"
#include "Renderer/Device/VulkanContext.h"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <span>

namespace batap
{
PassTimer::PassTimer(VulkanContext& ctx) : ctx_(ctx)
{
    if (platformEnvVar("BATAP_PASS_TIMES").value_or("0") == "0")
        return;
    VkQueryPoolCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    info.queryType = VK_QUERY_TYPE_TIMESTAMP;
    info.queryCount = kQueries * FramesInFlight;
    if (vkCreateQueryPool(ctx_.device_, &info, nullptr, &pool_) != VK_SUCCESS)
        pool_ = VK_NULL_HANDLE;
}

PassTimer::~PassTimer()
{
    vkDestroyQueryPool(ctx_.device_, pool_, nullptr);
}

void PassTimer::begin(VkCommandBuffer cmd, uint32_t frame)
{
    frame_ = frame;
    Slot& slot = slots_[frame];
    const uint32_t first = frame * kQueries;
    const auto count = static_cast<uint32_t>(slot.names_.size());
    std::vector<uint64_t> ticks(count);
    if (count > 1 &&
        vkGetQueryPoolResults(ctx_.device_, pool_, first, count, ticks.size() * sizeof(uint64_t),
                              ticks.data(), sizeof(uint64_t), VK_QUERY_RESULT_64_BIT) == VK_SUCCESS)
    {
        std::unordered_map<std::string, float> totals;
        std::vector<std::string> order;
        for (uint32_t i = 0; i + 1 < count; ++i)
        {
            const std::string& name = slot.names_[i];
            if (name.empty() || ticks[i + 1] < ticks[i])
                continue;
            const float ms =
                static_cast<float>(ticks[i + 1] - ticks[i]) * ctx_.timestampPeriodNs_ * 1e-6f;
            const auto [it, added] = totals.emplace(name, 0.f);
            if (added)
                order.push_back(name);
            it->second += ms;
        }
        for (const auto& [name, ms] : totals)
        {
            History& history = history_[name];
            history.ms_[history.count_++ % kHistory] = ms;
        }
        order_ = std::move(order);
    }
    slot.names_.clear();
    vkCmdResetQueryPool(cmd, pool_, first, kQueries);
}

void PassTimer::mark(VkCommandBuffer cmd, const std::string* pass)
{
    Slot& slot = slots_[frame_];
    if (slot.names_.size() >= kQueries)
        return;
    // Without a memory dependency, NVIDIA writes the timestamp before the dispatches before it
    // finish: their time lands in the next pass that has a barrier.
    VkMemoryBarrier2 drain{};
    drain.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    drain.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    drain.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    drain.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    drain.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    VkDependencyInfo dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.memoryBarrierCount = 1;
    dependency.pMemoryBarriers = &drain;
    vkCmdPipelineBarrier2(cmd, &dependency);
    vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, pool_,
                         frame_ * kQueries + static_cast<uint32_t>(slot.names_.size()));
    slot.names_.push_back(pass ? *pass : std::string());
}

void PassTimer::print() const
{
    if (!enabled() || order_.empty())
        return;
    float total = 0.f;
    std::cout << std::fixed << std::setprecision(3) << "[Vulkan] passes GPU, médianes :\n";
    for (const std::string& name : order_)
    {
        const History& history = history_.at(name);
        const std::span<const float> recorded =
            std::span(history.ms_).first(std::min(history.count_, kHistory));
        std::vector<float> times(recorded.begin(), recorded.end());
        const auto middle = times.begin() + static_cast<std::ptrdiff_t>(times.size() / 2);
        std::nth_element(times.begin(), middle, times.end());
        total += *middle;
        std::cout << "  " << name << " : " << *middle << " ms\n";
    }
    std::cout << "  total : " << total << " ms" << std::defaultfloat << std::endl;
}
}  // namespace batap
