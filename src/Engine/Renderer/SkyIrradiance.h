#pragma once

#include <Eigen/Core>
#include <array>

namespace batap
{

struct SH9 { std::array<Eigen::Vector3f, 9> c = {}; };

struct Skybox_C;  // forward declare

// Intégration numérique (Spherical Fibonacci, ~512 samples) — évalue la même
// formule sky que SkyPS.hlsl pour les modes FlatColor et Gradient.
SH9 projectSkyToSH(const Skybox_C& sky);

// Projection depuis des pixels RGBA float (HDR). Appelé au chargement
// avant de libérer les données CPU. Canal alpha ignoré.
SH9 projectHDRIToSH(const float* rgbaPixels, int w, int h);

}  // namespace batap
