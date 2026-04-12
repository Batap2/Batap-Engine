#pragma once

#include "Assets/AssetHandle.h"
#include <Eigen/Core>

namespace batap
{
struct Skybox_C
{
    enum class Mode : uint32_t { HDRI = 0, FlatColor = 1, Gradient = 2 };

    Mode            mode_         = Mode::HDRI;
    TextureHandle   hdri_         = {};
    Eigen::Vector3f color1_       = {0.5f, 0.72f, 0.90f};  // ciel (zenith)
    Eigen::Vector3f color2_       = {0.80f, 0.88f, 1.00f};  // horizon
    Eigen::Vector3f color3_       = {0.25f, 0.20f, 0.15f};  // bas (nadir)
    float           horizonWidth_ = 0.15f;                   // largeur de la bande horizon [0.01, 1]
    float           intensity_    = 1.0f;
};
}  // namespace batap
