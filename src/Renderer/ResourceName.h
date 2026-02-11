#pragma once

#include <magic_enum/magic_enum.hpp>
#include <string_view>

namespace batap
{
enum class RN
{
    // ID3D12Resources
    texture2D_backbuffers,
    texture2D_render0,
    texture2D_render3D,
    texture2D_render3D_depthStencil,
    buffer_camera,

    // Resource Views
    UAV_render0,
    CBV_camera,
    RTV_imgui,
    RTV_render_3d,
    DSV_render_3d,

    // Render Passes
    pass_render0,
    pass_geometry,
    pass_composition,
    pass_imgui,

    // Shaders
    shader_compute0,
    shader_3D_VS,
    shader_3D_PS,

    // PSOs
    pso_compute0,
    pso_geometry_pass
};

inline const std::string& toS(RN n)
{
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wexit-time-destructors"
    static const auto names = []
    {
        std::array<std::string, magic_enum::enum_count<RN>()> arr{};
        size_t i = 0;
        for (auto r : magic_enum::enum_values<RN>())
            arr[i++] = std::string(magic_enum::enum_name(r));
        return arr;
    }();
    return names[static_cast<size_t>(n)];
#pragma clang diagnostic push
}
}  // namespace batap
