#include "SkyIrradiance.h"

#include "Components/Skybox_C.h"

#include <cmath>
#include <numbers>

namespace batap
{

// ---------------------------------------------------------------------------
// Convention SH : y-up (axe vertical = y)
// Ordre : L00, L1-1(y), L10(z), L11(x), L2-2(xy), L2-1(yz), L20(3y²-1), L21(xz), L22(x²-z²)
// Normalisations réelles (sans le facteur de convolution Lambertien — il est
// dans EvalSH9 côté shader).

static void accumSH(SH9& sh, const Eigen::Vector3f& color, float x, float y, float z, float weight)
{
    sh.c[0] += color * (0.282095f                       * weight);
    sh.c[1] += color * (0.488603f * y                   * weight);
    sh.c[2] += color * (0.488603f * z                   * weight);
    sh.c[3] += color * (0.488603f * x                   * weight);
    sh.c[4] += color * (1.092548f * x * y               * weight);
    sh.c[5] += color * (1.092548f * y * z               * weight);
    sh.c[6] += color * (0.315392f * (3.f * y*y - 1.f)  * weight);
    sh.c[7] += color * (1.092548f * x * z               * weight);
    sh.c[8] += color * (0.546274f * (x*x - z*z)         * weight);
}

// Évalue la formule gradient du shader (même logique que SkyPS.hlsl)
static Eigen::Vector3f evalSkyColor(const Skybox_C& sky, float x, float y, float z)
{
    if (sky.mode_ == Skybox_C::Mode::FlatColor)
        return sky.color1_;

    // Gradient : smoothstep(0, hw, y) → lerp(horizon, sky, t_up)
    //            smoothstep(0, hw, -y) → lerp(..., ground, t_down)
    const float hw       = std::max(sky.horizonWidth_, 0.01f);
    const float t_up     = std::clamp(y / hw, 0.f, 1.f);
    const float t_up_s   = t_up * t_up * (3.f - 2.f * t_up);   // smoothstep
    const float t_down   = std::clamp(-y / hw, 0.f, 1.f);
    const float t_down_s = t_down * t_down * (3.f - 2.f * t_down);

    Eigen::Vector3f col = sky.color2_ + (sky.color1_ - sky.color2_) * t_up_s;
    col                 = col + (sky.color3_ - col) * t_down_s;
    return col;
}

// ---------------------------------------------------------------------------

SH9 projectSkyToSH(const Skybox_C& sky)
{
    SH9 sh;
    constexpr size_t N            = 512;
    constexpr float kGoldenAngle = std::numbers::pi_v<float> * (3.f - 1.6180339887f * 2.f);
    constexpr float kWeight = 4.f * std::numbers::pi_v<float> / static_cast<float>(N);

    for (size_t i = 0; i < N; ++i)
    {
        const float fi    = static_cast<float>(i);
        const float yi    = 1.f - (2.f * fi + 1.f) / static_cast<float>(N);
        const float r     = std::sqrt(std::max(0.f, 1.f - yi * yi));
        const float theta = kGoldenAngle * fi;
        const float xi    = std::cos(theta) * r;
        const float zi    = std::sin(theta) * r;

        Eigen::Vector3f color = evalSkyColor(sky, xi, yi, zi);
        accumSH(sh, color, xi, yi, zi, kWeight);
    }
    return sh;
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage"
SH9 projectHDRIToSH(const float* rgba, int w, int h)
{
    SH9 sh;
    const float fw = static_cast<float>(w);
    const float fh = static_cast<float>(h);
    const float dphi   = 2.f * std::numbers::pi_v<float> / fw;
    const float dtheta = std::numbers::pi_v<float> / fh;

    for (int row = 0; row < h; ++row)
    {
        const float theta = (static_cast<float>(row) + 0.5f) * dtheta;
        const float sinT  = std::sin(theta);
        const float cosT  = std::cos(theta);

        for (int col = 0; col < w; ++col)
        {
            const float phi = (static_cast<float>(col) + 0.5f) * dphi - std::numbers::pi_v<float>;
            const float xi  = sinT * std::cos(phi);
            const float zi  = sinT * std::sin(phi);
            const float yi  = cosT;

            const float* px = rgba + (row * w + col) * 4;
            Eigen::Vector3f color(px[0], px[1], px[2]);

            const float weight = sinT * dphi * dtheta;
            accumSH(sh, color, xi, yi, zi, weight);
        }
    }
    return sh;
}
#pragma clang diagnostic pop

}  // namespace batap
