#include "Renderer/Billboards.h"

#include "Shaders/ShaderInterop.h"

#include <algorithm>

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

void Billboards::endFrame(float dt)
{
    time_ += dt;
    std::erase_if(records_, [this](const Record& r) { return r.expiry_ <= time_; });
}

}  // namespace batap
