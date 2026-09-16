#include "GameModule.h"

#include "imgui.h"

namespace batap
{

void adoptHostImGui(const GameModuleAPI& api)
{
    if (!api.imguiContext_)
        return;

    if (api.imguiAlloc_ && api.imguiFree_)
        ImGui::SetAllocatorFunctions(api.imguiAlloc_, api.imguiFree_, api.imguiUserData_);

    ImGui::SetCurrentContext(api.imguiContext_);
}

}  // namespace batap
