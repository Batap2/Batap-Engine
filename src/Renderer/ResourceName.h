#pragma once

#include <magic_enum/magic_enum.hpp>
#include <string_view>

namespace rayvox
{
enum class RN
{
    // ID3D12Resources
    texture2D_backbuffers,
    texture2D_render0,
    buffer_camera,

    // Resource Views
    UAV_render0,
    CBV_camera,
    RTV_imgui,

    // Render Passes
    pass_render0,
    pass_mesh,
    pass_composition,
    pass_imgui,

    // Shaders
    shader_compute0,

    // PSOs
    pso_compute0,
};

inline const std::string& toS(RN n)
{
    static const auto names = []
    {
        std::array<std::string, magic_enum::enum_count<RN>()> arr{};
        size_t i = 0;
        for (auto r : magic_enum::enum_values<RN>())
            arr[i++] = std::string(magic_enum::enum_name(r));
        return arr;
    }();
    return names[static_cast<size_t>(n)];
}
}  // namespace rayvox