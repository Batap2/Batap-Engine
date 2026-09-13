#pragma once

#include "EigenTypes.h"
#include "Renderer/DebugDraw.h"
#include "Spatial/Ray.h"

#include <cstdint>
#include <vector>

namespace batap
{

struct Billboards
{
    enum class SizeMode : uint32_t
    {
        World = 0,   // size_ is in world units
        Screen = 1,  // size_ is a fraction of the viewport height
    };

    enum class Orientation : uint32_t
    {
        Spherical = 0,    // the quad faces the camera plane
        Cylindrical = 1,  // the quad only turns around Y
        None = 2,         // the quad keeps rot_, the camera is ignored
    };

    struct Desc
    {
        v3f pos_ = v3f::Zero();
        quatf rot_ = quatf::Identity();
        v2f size_ = {1.f, 1.f};
        col3 tint_ = colors::white;
        float alpha_ = 1.f;
        // Overrides the material's albedo map when set.
        uint32_t textureIdx_ = 0xFFFFFFFFu;
        // Material arena slot. Unset falls back to the default material.
        uint32_t materialIdx_ = 0xFFFFFFFFu;
        SizeMode sizeMode_ = SizeMode::World;
        Orientation orientation_ = Orientation::Spherical;
        float seconds_ = 0.f;
    };

    struct Record
    {
        v3f pos_;
        quatf rot_;
        v2f size_;
        col3 tint_;
        float alpha_;
        uint32_t textureIdx_;
        uint32_t materialIdx_;
        uint32_t flags_;
        float expiry_;
    };

    void add(const Desc& desc);

    // Call after the frame has been handed to the renderer.
    void endFrame(float dt);

    const std::vector<Record>& records() const { return records_; }

   private:
    float time_ = 0.f;
    std::vector<Record> records_;
};

struct CameraBasis
{
    v3f pos_ = v3f::Zero();
    v3f right_ = v3f::UnitX();
    v3f up_ = v3f::UnitY();
    float fov_ = 1.f;
};

struct BillboardQuad
{
    v3f center_ = v3f::Zero();
    v3f right_ = v3f::UnitX();
    v3f up_ = v3f::UnitY();
    float halfWidth_ = 0.f;
    float halfHeight_ = 0.f;

    v3f corner(float u, float v) const
    {
        return center_ + right_ * (u * halfWidth_) + up_ * (v * halfHeight_);
    }
};

// Mirrors BillboardVS.hlsl, which cannot share it: ShaderInterop's float3 is a
// bare array. Diverge and a billboard stops being picked where it is drawn.
BillboardQuad billboardQuad(const v3f& pos, const quatf& rot, const v2f& size,
                            Billboards::SizeMode sizeMode, Billboards::Orientation orientation,
                            const CameraBasis& cam);

struct QuadHit
{
    float t_ = 0.f;
    v3f point_ = v3f::Zero();
    v3f normal_ = v3f::Zero();
    bool hit_ = false;
};

QuadHit rayQuad(const Ray& ray, const BillboardQuad& quad, float maxT);

}  // namespace batap
