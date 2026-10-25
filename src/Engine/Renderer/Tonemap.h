#pragma once

#include "Shaders/ShaderInterop.h"

#include <array>
#include <string_view>

namespace batap
{
struct TonemapChoice
{
    std::string_view key_;
    std::string_view label_;
    TonemapOperator operator_;
};

inline constexpr std::array<TonemapChoice, 6> kTonemaps = {{
    {"agx", "AgX", TonemapAgx},
    {"pbrNeutral", "Khronos PBR Neutral", TonemapPbrNeutral},
    {"aces", "ACES (Hill fit)", TonemapAces},
    {"reinhard", "Reinhard", TonemapReinhard},
    {"uchimura", "Uchimura (Gran Turismo)", TonemapUchimura},
    {"none", "None (clip)", TonemapNone},
}};

inline const TonemapChoice& tonemapChoice(std::string_view key)
{
    for (const TonemapChoice& choice : kTonemaps)
        if (choice.key_ == key)
            return choice;
    return kTonemaps[0];
}
}  // namespace batap
