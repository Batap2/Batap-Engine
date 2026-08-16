#pragma once

#include <cstdint>

namespace batap
{
struct Material
{
    float albedo[4] = {1.f, 1.f, 1.f, 1.f};
    float roughness = 0.3f;
    float metallic = 0.f;
    float reflectivity = 0.f;
    uint32_t albedoTexIdx_ = 0xFFFFFFFFu;
    uint32_t normalTexIdx_ = 0xFFFFFFFFu;
    uint32_t roughnessTexIdx_ = 0xFFFFFFFFu;
    uint32_t metallicTexIdx_ = 0xFFFFFFFFu;
    uint32_t pad_ = {};
};
static_assert(sizeof(Material) % 16 == 0);
}  // namespace batap
