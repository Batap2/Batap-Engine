#pragma once

#include "Assets/AssetHandle.h"
#include "EigenTypes.h"
#include "ViewRect.h"

#include <entt/entt.hpp>

#include <array>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

namespace batap
{

struct ScatterDraws
{
    struct Instance
    {
        v3f position_ = v3f::Zero();
        quatf rotation_ = quatf::Identity();
        float scale_ = 1.f;
    };

    struct Batch
    {
        MeshHandle mesh_;
        MeshHandle shadowMesh_;
        std::array<MaterialHandle, 8> materials_{};
        std::vector<Instance> instances_;
        entt::entity owner_ = entt::null;
        v3d centre_ = v3d::Zero();
        bool castShadows_ = true;
        // Metres past its bounds, along the light, that the batch's shadow can
        // reach a receiver nothing else shadows; 0 for no bound.
        float shadowLength_ = 0.f;
        float fadeStart_ = 0.f;
        float fadeEnd_ = 0.f;
        float radius_ = 0.f;
        float impostorRadius_ = 0.f;
    };

    struct Placement
    {
        v3f offset_ = v3f::Zero();
        v3d centreInOwner_;
        uint32_t owner_ = 0;
        float radius_ = 0.f;
        bool visible_ = true;
    };

    // This frame, per owner of placed batches: its linear part, its axes and
    // the camera in metres along them.
    struct Owner
    {
        uint32_t owner_ = 0;
        m3f linear_ = m3f::Identity();
        m3f axes_ = m3f::Identity();
        v3d camera_ = v3d::Zero();
    };

    uint64_t add(Batch batch);
    void remove(uint64_t batch);
    void clear();
    void place(const entt::registry& reg, entt::entity camera, const ViewRect& view);
    size_t size() const { return anchors_.size(); }
    bool has(uint64_t batch) const { return anchors_.contains(batch); }

    std::vector<std::pair<uint64_t, Batch>> added_;
    std::vector<uint64_t> removed_;
    std::vector<std::pair<uint64_t, Placement>> placed_;
    std::vector<Owner> owners_;

   private:
    struct Anchor
    {
        entt::entity owner_ = entt::null;
        v3d centre_ = v3d::Zero();
        float radius_ = 0.f;
        bool castShadows_ = true;
    };

    std::unordered_map<uint64_t, Anchor> anchors_;
    uint64_t next_ = 1;
};

}  // namespace batap
