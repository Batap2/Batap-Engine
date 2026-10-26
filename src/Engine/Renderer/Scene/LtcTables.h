#pragma once

#include <array>
#include <cstdint>

namespace batap
{
// Indexed by (roughness, sqrt(1 - N.V)).
// LtcMatTable: M^-1 as (m00, m02, m20, m22), m11 = 1, others 0, in the frame
// (T1, T2, N) with T1 = V projected on the tangent plane.
// LtcAmpTable: x GGX albedo at F0 = 1, y its F90 part, w the clipped-sphere
// form factor.
constexpr uint32_t LtcTableSize = 64;
constexpr uint32_t LtcTableTexels = LtcTableSize * LtcTableSize;

extern const std::array<uint16_t, LtcTableTexels * 4> LtcMatTable;
extern const std::array<uint16_t, LtcTableTexels * 4> LtcAmpTable;
}  // namespace batap
