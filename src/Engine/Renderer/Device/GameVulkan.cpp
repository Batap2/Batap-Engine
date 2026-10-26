#include "GameModule.h"

namespace batap
{

void adoptHostVulkan(const GameModuleAPI& api)
{
    if (!api.vkGetInstanceProcAddr_ || !api.vkInstance_ || !api.vkDevice_)
        return;

    volkInitializeCustom(api.vkGetInstanceProcAddr_);
    volkLoadInstanceOnly(api.vkInstance_);
    volkLoadDevice(api.vkDevice_);
}

}  // namespace batap
