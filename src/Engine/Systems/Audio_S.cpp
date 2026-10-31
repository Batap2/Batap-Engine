#include "Audio_S.h"

#include "Audio/Audio.h"
#include "Components/AudioSource_C.h"
#include "Components/Transform_C.h"
#include "World.h"

namespace batap
{
void Audio_S::connectHooks(entt::registry& reg)
{
    reg.on_destroy<AudioSource_C>().connect<&Audio_S::onDestroy>(*this);
}

void Audio_S::onDestroy(entt::registry& reg, entt::entity e)
{
    if (World* const* world = reg.ctx().find<World*>())
        (*world)->audio().forgetSource(e);
}

void Audio_S::update(World& world, float dt)
{
    Audio& audio = world.audio();
    if (!audio.available())
        return;

    auto& reg = world.registry_;
    if (const entt::entity cam = world.renderCamera(); cam != entt::null)
    {
        const Transform_C& tc = reg.get<Transform_C>(cam);
        audio.updateListener(tc.worldPos(), tc.worldRot(), dt);
    }

    for (auto [e, source] : reg.view<AudioSource_C>().each())
        audio.syncSource({&reg, e}, source);

    audio.updateFollows(dt);
}
}  // namespace batap
