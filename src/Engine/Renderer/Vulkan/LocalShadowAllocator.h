#pragma once

#include "EigenTypes.h"
#include "Renderer/Vulkan/Passes/ShadowPass.h"

#include <entt/entt.hpp>

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace batap
{
struct GPUInstanceManager;

struct LocalShadowAllocator
{
    void allocate(entt::registry& reg, GPUInstanceManager& instances, entt::entity camera,
                  v2i frameSize, std::vector<ShadowView>& views);

   private:
    struct LightView
    {
        v3f forward_;
        v3f up_;
        uint32_t entry_ = 0;
    };

    struct Candidate
    {
        entt::entity entity_ = entt::null;
        v3f eye_;
        float zfar_ = 0.f;
        float fov_ = 0.f;
        float strength_ = 0.f;
        float priority_ = 0.f;
        // Lowered for a light the budget already shrank last frame, so two
        // lights of close priority do not trade their classes every frame.
        float shrinkPriority_ = 0.f;
        uint32_t requested_ = 0;
        uint32_t size_ = 0;
        uint32_t firstEntry_ = 0;
        // What the budget counts: every view, visible or not, so that turning
        // the camera never changes a class. Only the visible ones are drawn.
        uint32_t viewTotal_ = 0;
        uint32_t viewCount_ = 0;
        std::array<LightView, 6> views_{};
    };

    struct Tile
    {
        const Candidate* light_ = nullptr;
        const LightView* view_ = nullptr;
    };

    struct History
    {
        uint32_t requested_ = 0;
        bool shrunk_ = false;
    };

    std::vector<Candidate> candidates_;
    std::vector<Tile> tiles_;
    // The only state that outlives a frame, for the two hystereses.
    std::unordered_map<entt::entity, History> history_;
    std::unordered_map<entt::entity, History> nextHistory_;
};
}  // namespace batap
