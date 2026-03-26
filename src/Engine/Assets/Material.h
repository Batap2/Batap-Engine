#pragma once

namespace batap
{
struct Material
{
    float albedo[4] = {1.f, 1.f, 1.f, 1.f};
    float roughness = 0.5f;
    float metallic = 0.f;
    float _pad[2] = {};
};
static_assert(sizeof(Material) % 16 == 0);
}  // namespace batap
