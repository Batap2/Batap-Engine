#pragma once

#include <cstddef>
#include <cstdint>

namespace batap
{
enum class AudioBus : uint32_t
{
    Sfx,
    Music,
    Ui,
};
inline constexpr size_t kAudioBusCount = 3;

enum class Attenuation : uint32_t
{
    Inverse,
    Linear,
    Exponential,
    None,
};

struct VoiceHandle
{
    uint32_t index = 0;
    uint32_t generation = 0;

    friend bool operator==(VoiceHandle, VoiceHandle) = default;
    explicit operator bool() const { return generation != 0; }
};
}  // namespace batap
