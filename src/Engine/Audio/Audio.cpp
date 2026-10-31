#include "Audio.h"

#include "Assets/AssetLoader.h"
#include "Assets/AssetManager.h"
#include "Assets/Sound.h"
#include "Components/AudioSource_C.h"
#include "Components/Transform_C.h"
#include "Platform/PlatformWindow.h"

#include <miniaudio/miniaudio.h>

#include <algorithm>
#include <array>
#include <iostream>
#include <random>
#include <unordered_map>

namespace batap
{
namespace
{
constexpr uint32_t kMaxVoices = 128;
constexpr uint64_t kMaxDecodedSeconds = 20;

struct GeneratorSource
{
    ma_data_source_base base_;
    Audio::Generator* fn_ = nullptr;
    uint32_t channels_ = 0;
    uint32_t sampleRate_ = 0;
    bool ended_ = false;
};

template <class T>
void store(T* out, T value)
{
    if (out == nullptr)
        return;
    T& slot = *out;
    slot = value;
}

ma_result generatorRead(ma_data_source* ds, void* out, ma_uint64 frameCount, ma_uint64* framesRead)
{
    auto& src = *static_cast<GeneratorSource*>(ds);
    store(framesRead, ma_uint64{0});
    if (src.ended_)
        return MA_AT_END;
    if (out)
    {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage-in-container"
        const std::span<float> interleaved(static_cast<float*>(out),
                                           static_cast<size_t>(frameCount) * src.channels_);
#pragma clang diagnostic pop
        src.ended_ = !(*src.fn_)(interleaved);
    }
    store(framesRead, frameCount);
    return MA_SUCCESS;
}

ma_result generatorSeek(ma_data_source*, ma_uint64)
{
    return MA_NOT_IMPLEMENTED;
}

ma_result generatorFormat(ma_data_source* ds, ma_format* format, ma_uint32* channels,
                          ma_uint32* sampleRate, ma_channel* channelMap, size_t channelMapCap)
{
    const auto& src = *static_cast<const GeneratorSource*>(ds);
    store(format, ma_format_f32);
    store(channels, ma_uint32{src.channels_});
    store(sampleRate, ma_uint32{src.sampleRate_});
    ma_channel_map_init_standard(ma_standard_channel_map_default, channelMap, channelMapCap,
                                 src.channels_);
    return MA_SUCCESS;
}

const ma_data_source_vtable kGeneratorVtable = {
    generatorRead, generatorSeek, generatorFormat, nullptr, nullptr, nullptr, 0};

ma_attenuation_model toMa(Attenuation attenuation)
{
    switch (attenuation)
    {
        case Attenuation::Inverse:
            return ma_attenuation_model_inverse;
        case Attenuation::Linear:
            return ma_attenuation_model_linear;
        case Attenuation::Exponential:
            return ma_attenuation_model_exponential;
        case Attenuation::None:
            break;
    }
    return ma_attenuation_model_none;
}

ma_result openDecoder(const std::filesystem::path& file, const ma_decoder_config& config,
                      ma_decoder& decoder)
{
#if defined(_WIN32)
    return ma_decoder_init_file_w(file.c_str(), &config, &decoder);
#else
    return ma_decoder_init_file(file.c_str(), &config, &decoder);
#endif
}

ma_result initFromFile(ma_engine& engine, const std::filesystem::path& file, ma_uint32 flags,
                       ma_sound_group& group, ma_sound& sound)
{
#if defined(_WIN32)
    return ma_sound_init_from_file_w(&engine, file.c_str(), flags, &group, nullptr, &sound);
#else
    return ma_sound_init_from_file(&engine, file.c_str(), flags, &group, nullptr, &sound);
#endif
}

float jitter(std::minstd_rand& rng, float amount)
{
    if (amount <= 0.f)
        return 1.f;
    std::uniform_real_distribution<float> spread(-amount, amount);
    return std::max(0.f, 1.f + spread(rng));
}

PlayDesc descOf(const AudioSource_C& c)
{
    return {.volume = c.volume_,
            .pitch = c.pitch_,
            .bus = c.bus_,
            .loop = c.loop_,
            .minDistance = c.minDistance_,
            .maxDistance = c.maxDistance_,
            .attenuation = c.attenuation_,
            .rolloff = c.rolloff_,
            .doppler = c.doppler_};
}

std::optional<v3f> worldPosOf(const EntityHandle& h)
{
    const auto* tc = h.try_get<Transform_C>();
    if (!tc)
        return std::nullopt;
    return tc->worldPos();
}
}  // namespace

struct Audio::Voice
{
    ma_sound sound_{};
    ma_audio_buffer_ref buffer_{};
    GeneratorSource generatorSource_{};
    Generator generator_;
    std::vector<float> samples_;
    SoundHandle asset_;
    EntityHandle follow_;
    v3f lastPos_ = v3f::Zero();
    bool hasLastPos_ = false;
    bool soundReady_ = false;
    bool usesBuffer_ = false;
    bool usesGenerator_ = false;
    bool loop_ = false;
    bool doppler_ = false;
};

struct Audio::Impl
{
    AssetManager* assets_ = nullptr;
    ma_engine engine_{};
    std::array<ma_sound_group, kAudioBusCount> buses_{};
    std::array<float, kAudioBusCount> busVolumes_{1.f, 1.f, 1.f};
    float masterVolume_ = 1.f;
    bool available_ = false;
    bool warnedFull_ = false;

    std::array<std::unique_ptr<Voice>, kMaxVoices> voices_;
    std::array<uint32_t, kMaxVoices> generations_{};
    std::unordered_map<entt::entity, VoiceHandle> sources_;
    std::minstd_rand rng_{0x5eedu};

    v3f listenerPos_ = v3f::Zero();
    bool hasListenerPos_ = false;
};

Audio::Audio(AssetManager& assets) : impl_(std::make_unique<Impl>())
{
    impl_->assets_ = &assets;
    if (const auto off = platformEnvVar("BATAP_NO_AUDIO"); off && *off != "0")
        return;

    const ma_engine_config config = ma_engine_config_init();
    if (ma_engine_init(&config, &impl_->engine_) != MA_SUCCESS)
    {
        std::cerr << "[Audio] No audio device, sound is off\n";
        return;
    }

    size_t ready = 0;
    for (; ready < kAudioBusCount; ++ready)
        if (ma_sound_group_init(&impl_->engine_, 0, nullptr, &impl_->buses_[ready]) != MA_SUCCESS)
            break;
    if (ready < kAudioBusCount)
    {
        std::cerr << "[Audio] Could not create the mix buses, sound is off\n";
        for (size_t i = 0; i < ready; ++i)
            ma_sound_group_uninit(&impl_->buses_[i]);
        ma_engine_uninit(&impl_->engine_);
        return;
    }
    impl_->available_ = true;
}

Audio::~Audio()
{
    if (!impl_->available_)
        return;
    stopAll();
    for (ma_sound_group& bus : impl_->buses_)
        ma_sound_group_uninit(&bus);
    ma_engine_uninit(&impl_->engine_);
}

bool Audio::available() const
{
    return impl_->available_;
}

Audio::Voice* Audio::voice(VoiceHandle handle) const
{
    if (!handle || handle.index >= kMaxVoices ||
        impl_->generations_[handle.index] != handle.generation)
        return nullptr;
    return impl_->voices_[handle.index].get();
}

std::pair<VoiceHandle, Audio::Voice*> Audio::allocate()
{
    for (uint32_t i = 0; i < kMaxVoices; ++i)
    {
        if (impl_->voices_[i])
            continue;
        uint32_t& generation = impl_->generations_[i];
        generation = generation + 1 == 0 ? 1 : generation + 1;
        impl_->voices_[i] = std::make_unique<Voice>();
        return {{i, generation}, impl_->voices_[i].get()};
    }
    if (!impl_->warnedFull_)
    {
        std::cerr << "[Audio] " << kMaxVoices << " voices playing, new sounds are dropped\n";
        impl_->warnedFull_ = true;
    }
    return {};
}

void Audio::release(uint32_t index)
{
    std::unique_ptr<Voice>& v = impl_->voices_[index];
    if (!v)
        return;
    if (v->soundReady_)
        ma_sound_uninit(&v->sound_);
    if (v->usesBuffer_)
        ma_audio_buffer_ref_uninit(&v->buffer_);
    if (v->usesGenerator_)
        ma_data_source_uninit(&v->generatorSource_.base_);
    v.reset();
}

VoiceHandle Audio::start(VoiceHandle handle, Voice& v, const PlayDesc& desc)
{
    ma_sound& s = v.sound_;
    v.loop_ = desc.loop;
    v.doppler_ = desc.doppler;
    v.follow_ = desc.follow;

    ma_sound_set_volume(&s, desc.volume * jitter(impl_->rng_, desc.volumeJitter));
    ma_sound_set_pitch(&s, desc.pitch * jitter(impl_->rng_, desc.pitchJitter));
    ma_sound_set_looping(&s, desc.loop ? MA_TRUE : MA_FALSE);

    const std::optional<v3f> pos = desc.follow.reg_ ? worldPosOf(desc.follow) : desc.at;
    const bool spatial = desc.follow.reg_ || desc.at;
    ma_sound_set_spatialization_enabled(&s, spatial ? MA_TRUE : MA_FALSE);
    if (spatial)
    {
        const v3f p = pos.value_or(v3f::Zero());
        ma_sound_set_position(&s, p.x(), p.y(), p.z());
        ma_sound_set_min_distance(&s, desc.minDistance);
        ma_sound_set_max_distance(&s, desc.maxDistance);
        ma_sound_set_attenuation_model(&s, toMa(desc.attenuation));
        ma_sound_set_rolloff(&s, desc.rolloff);
        ma_sound_set_doppler_factor(&s, desc.doppler ? 1.f : 0.f);
        v.lastPos_ = p;
        v.hasLastPos_ = true;
    }

    if (ma_sound_start(&s) != MA_SUCCESS)
    {
        release(handle.index);
        return {};
    }
    return handle;
}

VoiceHandle Audio::play(std::string_view path, const PlayDesc& desc)
{
    if (!impl_->available_)
        return {};
    const SoundHandle sound = loadAsset<Sound>(normalizeAssetPath(path), *impl_->assets_);
    if (!sound)
        std::cerr << "[Audio] Cannot play " << path << "\n";
    return play(sound, desc);
}

VoiceHandle Audio::play(SoundHandle sound, const PlayDesc& desc)
{
    if (!impl_->available_)
        return {};
    const Sound* data = impl_->assets_->get(sound);
    if (!data)
        return {};

    auto [handle, v] = allocate();
    if (!v)
        return {};
    v->asset_ = sound;

    ma_sound_group& bus = impl_->buses_[static_cast<size_t>(desc.bus)];
    ma_result result = MA_ERROR;
    if (data->streamed())
        result = initFromFile(impl_->engine_, data->file_, MA_SOUND_FLAG_STREAM, bus, v->sound_);
    else
    {
        ma_audio_buffer_ref_init(ma_format_f32, data->channels_, data->samples_.data(),
                                 data->frames_, &v->buffer_);
        v->buffer_.sampleRate = data->sampleRate_;
        v->usesBuffer_ = true;
        result = ma_sound_init_from_data_source(&impl_->engine_, &v->buffer_, 0, &bus, &v->sound_);
    }
    if (result != MA_SUCCESS)
    {
        release(handle.index);
        return {};
    }
    v->soundReady_ = true;
    return start(handle, *v, desc);
}

VoiceHandle Audio::play(std::vector<float> samples, uint32_t channels, uint32_t sampleRate,
                        const PlayDesc& desc)
{
    if (!impl_->available_ || channels == 0 || sampleRate == 0 || samples.size() < channels)
        return {};

    auto [handle, v] = allocate();
    if (!v)
        return {};
    v->samples_ = std::move(samples);

    ma_audio_buffer_ref_init(ma_format_f32, channels, v->samples_.data(),
                             v->samples_.size() / channels, &v->buffer_);
    v->buffer_.sampleRate = sampleRate;
    v->usesBuffer_ = true;
    ma_sound_group& bus = impl_->buses_[static_cast<size_t>(desc.bus)];
    if (ma_sound_init_from_data_source(&impl_->engine_, &v->buffer_, 0, &bus, &v->sound_) !=
        MA_SUCCESS)
    {
        release(handle.index);
        return {};
    }
    v->soundReady_ = true;
    return start(handle, *v, desc);
}

VoiceHandle Audio::play(Generator generator, uint32_t channels, uint32_t sampleRate,
                        const PlayDesc& desc)
{
    if (!impl_->available_ || !generator || channels == 0 || sampleRate == 0)
        return {};

    auto [handle, v] = allocate();
    if (!v)
        return {};
    v->generator_ = std::move(generator);

    GeneratorSource& src = v->generatorSource_;
    src.fn_ = &v->generator_;
    src.channels_ = channels;
    src.sampleRate_ = sampleRate;
    ma_data_source_config config = ma_data_source_config_init();
    config.vtable = &kGeneratorVtable;
    if (ma_data_source_init(&config, &src.base_) != MA_SUCCESS)
    {
        release(handle.index);
        return {};
    }
    v->usesGenerator_ = true;

    ma_sound_group& bus = impl_->buses_[static_cast<size_t>(desc.bus)];
    if (ma_sound_init_from_data_source(&impl_->engine_, &src, 0, &bus, &v->sound_) != MA_SUCCESS)
    {
        release(handle.index);
        return {};
    }
    v->soundReady_ = true;
    return start(handle, *v, desc);
}

SoundHandle Audio::createSound(std::string name, std::vector<float> samples, uint32_t channels,
                               uint32_t sampleRate)
{
    if (channels == 0 || sampleRate == 0 || samples.size() < channels)
        return {};
    Sound sound;
    sound.channels_ = channels;
    sound.sampleRate_ = sampleRate;
    sound.frames_ = samples.size() / channels;
    sound.samples_ = std::move(samples);
    return impl_->assets_->emplace<Sound>(std::move(name), std::string{}, std::move(sound)).first;
}

void Audio::stop(VoiceHandle handle)
{
    if (voice(handle))
        release(handle.index);
}

void Audio::setPaused(VoiceHandle handle, bool paused)
{
    if (Voice* v = voice(handle))
    {
        if (paused)
            ma_sound_stop(&v->sound_);
        else
            ma_sound_start(&v->sound_);
    }
}

bool Audio::playing(VoiceHandle handle) const
{
    const Voice* v = voice(handle);
    return v && ma_sound_is_playing(&v->sound_);
}

void Audio::setVolume(VoiceHandle handle, float volume)
{
    if (Voice* v = voice(handle))
        ma_sound_set_volume(&v->sound_, volume);
}

void Audio::setPitch(VoiceHandle handle, float pitch)
{
    if (Voice* v = voice(handle))
        ma_sound_set_pitch(&v->sound_, pitch);
}

void Audio::setPosition(VoiceHandle handle, const v3f& pos)
{
    if (Voice* v = voice(handle))
        ma_sound_set_position(&v->sound_, pos.x(), pos.y(), pos.z());
}

VoiceHandle Audio::playSource(EntityHandle source)
{
    const auto* c = source.try_get<AudioSource_C>();
    if (!c)
        return {};
    stopSource(source);

    PlayDesc desc = descOf(*c);
    if (c->spatial_)
        desc.follow = source;
    const VoiceHandle handle = play(c->sound_, desc);
    impl_->sources_[source.entity_] = handle;
    return handle;
}

void Audio::stopSource(EntityHandle source)
{
    if (const auto it = impl_->sources_.find(source.entity_); it != impl_->sources_.end())
        stop(it->second);
}

bool Audio::sourcePlaying(EntityHandle source) const
{
    const auto it = impl_->sources_.find(source.entity_);
    return it != impl_->sources_.end() && playing(it->second);
}

void Audio::syncSource(EntityHandle source, const AudioSource_C& c)
{
    const auto it = impl_->sources_.find(source.entity_);
    if (it == impl_->sources_.end())
    {
        if (c.autoplay_)
            playSource(source);
        return;
    }

    Voice* v = voice(it->second);
    if (!v)
        return;
    if (v->asset_ != c.sound_ || (v->follow_.reg_ != nullptr) != c.spatial_)
    {
        playSource(source);
        return;
    }

    ma_sound& s = v->sound_;
    ma_sound_set_volume(&s, c.volume_);
    ma_sound_set_pitch(&s, c.pitch_);
    ma_sound_set_looping(&s, c.loop_ ? MA_TRUE : MA_FALSE);
    ma_sound_set_min_distance(&s, c.minDistance_);
    ma_sound_set_max_distance(&s, c.maxDistance_);
    ma_sound_set_attenuation_model(&s, toMa(c.attenuation_));
    ma_sound_set_rolloff(&s, c.rolloff_);
    ma_sound_set_doppler_factor(&s, c.doppler_ ? 1.f : 0.f);
    v->loop_ = c.loop_;
    v->doppler_ = c.doppler_;
}

void Audio::forgetSource(entt::entity source)
{
    if (const auto it = impl_->sources_.find(source); it != impl_->sources_.end())
    {
        stop(it->second);
        impl_->sources_.erase(it);
    }
}

void Audio::setBusVolume(AudioBus bus, float volume)
{
    const auto i = static_cast<size_t>(bus);
    impl_->busVolumes_[i] = std::max(volume, 0.f);
    if (impl_->available_)
        ma_sound_group_set_volume(&impl_->buses_[i], impl_->busVolumes_[i]);
}

float Audio::busVolume(AudioBus bus) const
{
    return impl_->busVolumes_[static_cast<size_t>(bus)];
}

void Audio::setMasterVolume(float volume)
{
    impl_->masterVolume_ = std::max(volume, 0.f);
    if (impl_->available_)
        ma_engine_set_volume(&impl_->engine_, impl_->masterVolume_);
}

float Audio::masterVolume() const
{
    return impl_->masterVolume_;
}

void Audio::updateListener(const v3f& pos, const quatf& rot, float dt)
{
    if (!impl_->available_)
        return;
    const v3f velocity = impl_->hasListenerPos_ && dt > 0.f ? v3f((pos - impl_->listenerPos_) / dt)
                                                            : v3f::Zero();
    impl_->listenerPos_ = pos;
    impl_->hasListenerPos_ = true;

    const v3f forward = rot * -v3f::UnitZ();
    const v3f up = rot * v3f::UnitY();
    ma_engine& e = impl_->engine_;
    ma_engine_listener_set_position(&e, 0, pos.x(), pos.y(), pos.z());
    ma_engine_listener_set_direction(&e, 0, forward.x(), forward.y(), forward.z());
    ma_engine_listener_set_world_up(&e, 0, up.x(), up.y(), up.z());
    ma_engine_listener_set_velocity(&e, 0, velocity.x(), velocity.y(), velocity.z());
}

void Audio::updateFollows(float dt)
{
    for (uint32_t i = 0; i < kMaxVoices; ++i)
    {
        Voice* v = impl_->voices_[i].get();
        if (!v || !v->follow_.reg_)
            continue;

        const std::optional<v3f> pos = worldPosOf(v->follow_);
        if (!pos)
        {
            if (v->loop_)
                release(i);
            else
                v->follow_ = {};
            continue;
        }

        ma_sound_set_position(&v->sound_, pos->x(), pos->y(), pos->z());
        if (v->doppler_ && v->hasLastPos_ && dt > 0.f)
        {
            const v3f velocity = (*pos - v->lastPos_) / dt;
            ma_sound_set_velocity(&v->sound_, velocity.x(), velocity.y(), velocity.z());
        }
        v->lastPos_ = *pos;
        v->hasLastPos_ = true;
    }
}

void Audio::shift(const v3f& by)
{
    impl_->listenerPos_ += by;
    for (const std::unique_ptr<Voice>& v : impl_->voices_)
    {
        if (!v || !v->hasLastPos_)
            continue;
        v->lastPos_ += by;
        if (!v->follow_.reg_)
            ma_sound_set_position(&v->sound_, v->lastPos_.x(), v->lastPos_.y(), v->lastPos_.z());
    }
}

void Audio::stopAll()
{
    for (uint32_t i = 0; i < kMaxVoices; ++i)
        release(i);
    impl_->sources_.clear();
    impl_->hasListenerPos_ = false;
}

void Audio::update()
{
    for (uint32_t i = 0; i < kMaxVoices; ++i)
        if (const Voice* v = impl_->voices_[i].get(); v && ma_sound_at_end(&v->sound_))
            release(i);
}

std::optional<Sound> readSoundFile(const std::filesystem::path& file)
{
    const ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 0, 0);
    ma_decoder decoder;
    if (openDecoder(file, config, decoder) != MA_SUCCESS)
        return std::nullopt;

    Sound sound;
    sound.file_ = file;
    ma_format format = ma_format_unknown;
    ma_decoder_get_data_format(&decoder, &format, &sound.channels_, &sound.sampleRate_, nullptr, 0);
    if (sound.channels_ == 0 || sound.sampleRate_ == 0)
    {
        ma_decoder_uninit(&decoder);
        return std::nullopt;
    }

    const uint64_t limit = kMaxDecodedSeconds * sound.sampleRate_;
    constexpr uint64_t kChunk = 4096;
    uint64_t frames = 0;
    bool ended = false;
    while (frames <= limit)
    {
        sound.samples_.resize(static_cast<size_t>((frames + kChunk) * sound.channels_));
        ma_uint64 read = 0;
        const ma_result result = ma_decoder_read_pcm_frames(
            &decoder, &sound.samples_[static_cast<size_t>(frames * sound.channels_)], kChunk,
            &read);
        frames += read;
        if (result != MA_SUCCESS || read < kChunk)
        {
            ended = true;
            break;
        }
    }

    if (ended && frames > 0)
    {
        sound.samples_.resize(static_cast<size_t>(frames * sound.channels_));
        sound.samples_.shrink_to_fit();
        sound.frames_ = frames;
    }
    else
    {
        sound.samples_ = {};
        ma_uint64 length = 0;
        ma_decoder_get_length_in_pcm_frames(&decoder, &length);
        sound.frames_ = length;
    }
    ma_decoder_uninit(&decoder);
    if (ended && frames == 0)
        return std::nullopt;
    return sound;
}
}  // namespace batap
