#pragma once

#include "Renderer/EngineConfig.h"

#include <volk.h>

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace batap
{
struct VulkanContext;

struct PassTimer
{
    explicit PassTimer(VulkanContext& ctx);
    ~PassTimer();

    PassTimer(const PassTimer&) = delete;
    PassTimer& operator=(const PassTimer&) = delete;

    bool enabled() const { return pool_ != VK_NULL_HANDLE; }
    void begin(VkCommandBuffer cmd, uint32_t frame);
    void mark(VkCommandBuffer cmd, const std::string* pass);
    void print() const;

   private:
    static constexpr uint32_t kQueries = 512;
    static constexpr size_t kHistory = 256;

    struct Slot
    {
        std::vector<std::string> names_;
    };

    struct History
    {
        std::array<float, kHistory> ms_{};
        size_t count_ = 0;
    };

    VulkanContext& ctx_;
    VkQueryPool pool_ = VK_NULL_HANDLE;
    std::array<Slot, FramesInFlight> slots_{};
    uint32_t frame_ = 0;
    std::unordered_map<std::string, History> history_;
    std::vector<std::string> order_;
};
}  // namespace batap
