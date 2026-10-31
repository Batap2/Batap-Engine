#pragma once

#include "Assets/AssetHandle.h"
#include "Audio/AudioTypes.h"
#include "Reflection/ComponentRegistry.h"

namespace batap
{
struct AudioSource_C
{
    SoundHandle sound_;
    float volume_ = 1.f;
    float pitch_ = 1.f;
    bool loop_ = false;
    bool autoplay_ = true;
    bool spatial_ = true;
    AudioBus bus_ = AudioBus::Sfx;
    float minDistance_ = 1.f;
    float maxDistance_ = 100.f;
    Attenuation attenuation_ = Attenuation::Inverse;
    float rolloff_ = 1.f;
    bool doppler_ = false;
};

BATAP_COMPONENT(AudioSource_C, "audioSource", ComponentMeta{.color = ComponentColor::Green},
                fieldMeta<&AudioSource_C::volume_>({.speed = 0.01f, .min = 0.f, .max = 4.f}),
                fieldMeta<&AudioSource_C::pitch_>({.speed = 0.01f, .min = 0.01f, .max = 4.f}),
                fieldMeta<&AudioSource_C::minDistance_>({.speed = 0.1f, .min = 0.01f, .max = 1e6f}),
                fieldMeta<&AudioSource_C::maxDistance_>({.speed = 1.f, .min = 0.01f, .max = 1e6f}),
                fieldMeta<&AudioSource_C::rolloff_>({.speed = 0.01f, .min = 0.f, .max = 10.f}));
}  // namespace batap
