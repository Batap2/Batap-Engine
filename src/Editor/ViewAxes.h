#pragma once

#include "Components/EntityHandle.h"
#include "Components/FreeCamController_C.h"
#include "EigenTypes.h"

#include <imgui.h>

#include <array>

namespace batap
{
struct Engine;
struct World;

struct ViewAxes
{
    void draw(World& world, Engine& ctx, EntityHandle cam, ImVec2 viewMin, ImVec2 viewMax);

   private:
    void align(World& world, Engine& ctx, EntityHandle cam, FreeCamController_C& ctrl,
               const v3f& look);
    void animate(Engine& ctx, EntityHandle cam, FreeCamController_C& ctrl);
    void navigateOrtho(World& world, Engine& ctx, EntityHandle cam, ImVec2 viewMin, ImVec2 viewMax);

    v3f pivot_ = v3f::Zero();
    float distance_ = 0.f;
    float fromYaw_ = 0.f;
    float fromPitch_ = 0.f;
    float toYaw_ = 0.f;
    float toPitch_ = 0.f;
    float progress_ = 1.f;
    bool panning_ = false;
    std::array<float, 7> glow_{};
};
}  // namespace batap
