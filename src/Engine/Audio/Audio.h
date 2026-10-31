#pragma once

#include "Assets/AssetHandle.h"
#include "Audio/AudioTypes.h"
#include "Components/EntityHandle.h"
#include "EigenTypes.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace batap
{
struct AssetManager;
struct AudioSource_C;
struct Sound;

struct PlayDesc
{
    float volume = 1.f;
    float pitch = 1.f;
    AudioBus bus = AudioBus::Sfx;
    bool loop = false;
    std::optional<v3f> at;
    EntityHandle follow;
    float minDistance = 1.f;
    float maxDistance = 100.f;
    Attenuation attenuation = Attenuation::Inverse;
    float rolloff = 1.f;
    bool doppler = false;
    float volumeJitter = 0.f;
    float pitchJitter = 0.f;
};

struct Audio
{
    using Generator = std::function<bool(std::span<float> interleaved)>;

    explicit Audio(AssetManager& assets);
    ~Audio();

    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;

    bool available() const;

    VoiceHandle play(std::string_view path, const PlayDesc& desc = {});
    VoiceHandle play(SoundHandle sound, const PlayDesc& desc = {});
    VoiceHandle play(std::vector<float> samples, uint32_t channels, uint32_t sampleRate,
                     const PlayDesc& desc = {});
    VoiceHandle play(Generator generator, uint32_t channels, uint32_t sampleRate,
                     const PlayDesc& desc = {});
    SoundHandle createSound(std::string name, std::vector<float> samples, uint32_t channels,
                            uint32_t sampleRate);

    void stop(VoiceHandle voice);
    void setPaused(VoiceHandle voice, bool paused);
    bool playing(VoiceHandle voice) const;
    void setVolume(VoiceHandle voice, float volume);
    void setPitch(VoiceHandle voice, float pitch);
    void setPosition(VoiceHandle voice, const v3f& pos);

    VoiceHandle playSource(EntityHandle source);
    void stopSource(EntityHandle source);
    bool sourcePlaying(EntityHandle source) const;

    void setBusVolume(AudioBus bus, float volume);
    float busVolume(AudioBus bus) const;
    void setMasterVolume(float volume);
    float masterVolume() const;

    void syncSource(EntityHandle source, const AudioSource_C& component);
    void forgetSource(entt::entity source);
    void updateListener(const v3f& pos, const quatf& rot, float dt);
    void updateFollows(float dt);
    void shift(const v3f& by);
    void stopAll();
    void update();

   private:
    struct Impl;
    struct Voice;

    Voice* voice(VoiceHandle handle) const;
    std::pair<VoiceHandle, Voice*> allocate();
    void release(uint32_t index);
    VoiceHandle start(VoiceHandle handle, Voice& v, const PlayDesc& desc);

    std::unique_ptr<Impl> impl_;
};

std::optional<Sound> readSoundFile(const std::filesystem::path& file);
}  // namespace batap
