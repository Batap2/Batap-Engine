#include "Renderer/Billboards.h"

#include "Shaders/ShaderInterop.h"

#include <algorithm>
#include <cmath>

namespace batap
{

void Billboards::add(const Desc& desc)
{
    uint32_t flags = 0;
    if (desc.sizeMode_ == SizeMode::Screen)
        flags |= BillboardScreenSize;
    if (desc.orientation_ == Orientation::Cylindrical)
        flags |= BillboardCylindrical;
    if (desc.orientation_ == Orientation::None)
        flags |= BillboardFixed;

    records_.push_back({desc.pos_, desc.rot_, desc.size_, desc.tint_, desc.alpha_,
                        desc.textureIdx_, desc.materialIdx_, flags, time_ + desc.seconds_});
}

BillboardQuad billboardQuad(const v3f& pos, const quatf& rot, const v2f& size,
                            Billboards::SizeMode sizeMode, Billboards::Orientation orientation,
                            const CameraBasis& cam)
{
    const v3f toCam = cam.pos_ - pos;
    const float dist = std::max(toCam.norm(), 0.0001f);

    v3f right = cam.right_;
    v3f up = cam.up_;

    if (orientation == Billboards::Orientation::None)
    {
        right = rot * v3f::UnitX();
        up = rot * v3f::UnitY();
    }
    else if (orientation == Billboards::Orientation::Cylindrical)
    {
        const v3f horizontal = v3f::UnitY().cross(toCam / dist);
        if (horizontal.squaredNorm() > 1e-6f)
        {
            right = horizontal.normalized();
            up = v3f::UnitY();
        }
    }

    const float scale =
        sizeMode == Billboards::SizeMode::Screen ? dist * std::tan(cam.fov_ * 0.5f) : 0.5f;

    return {pos, right, up, size.x() * scale, size.y() * scale};
}

QuadHit rayQuad(const Ray& ray, const BillboardQuad& quad, float maxT)
{
    QuadHit out;

    const v3f normal = quad.right_.cross(quad.up_);
    const float denom = ray.dir_.dot(normal);
    if (std::abs(denom) < 1e-8f)
        return out;

    const float t = (quad.center_ - ray.origin_).dot(normal) / denom;
    if (t < ray.tMin_ || t >= maxT)
        return out;

    const v3f local = ray.origin_ + ray.dir_ * t - quad.center_;
    if (std::abs(local.dot(quad.right_)) > quad.halfWidth_ ||
        std::abs(local.dot(quad.up_)) > quad.halfHeight_)
        return out;

    out.t_ = t;
    out.point_ = ray.origin_ + ray.dir_ * t;
    out.normal_ = denom < 0.f ? normal : -normal;
    out.hit_ = true;
    return out;
}

void Billboards::endFrame(float dt)
{
    time_ += dt;
    std::erase_if(records_, [this](const Record& r) { return r.expiry_ <= time_; });
}

}  // namespace batap
