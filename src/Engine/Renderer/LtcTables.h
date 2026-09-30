#pragma once

#include <array>
#include <cstdint>

namespace batap
{
// The two lookup tables of linearly transformed cosines (Heitz et al. 2016)
// for GGX, 64x64 RGBA16F each, indexed by (roughness, sqrt(1 - N.V)).
//
// LtcMatTable: the inverse transform M^-1, as (m00, m02, m20, m22) with m11 = 1
// and the other terms 0, in the frame (T1, T2, N) where T1 is V projected on
// the tangent plane.
// LtcAmpTable: x the GGX albedo at F0 = 1, y its part that goes to F90
// (Hill's Fresnel split), w the clipped-sphere form factor term.
constexpr uint32_t LtcTableSize = 64;
constexpr uint32_t LtcTableTexels = LtcTableSize * LtcTableSize;

extern const std::array<uint16_t, LtcTableTexels * 4> LtcMatTable;
extern const std::array<uint16_t, LtcTableTexels * 4> LtcAmpTable;
}  // namespace batap
