// The game's single miniaudio implementation TU. Full feature set: the
// resource manager / engine / node graph for AudioSystem, the built-in MP3
// decoder for the .mp3 assets, and the raw device API for the synthesized
// ambience in Audio.cpp (which includes <miniaudio.h> declarations only).
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING
#include "AudioSystem.hpp"

namespace bk {

namespace {
// Same resolution rule as the renderer's asset loading.
std::string resolveAssetPath(const std::string& path) {
    return path.empty() || path.front() == '/' ? path : vke::assetPath(path);
}
} // namespace

// ---------------------------------------------------------------- AudioSystem

AudioSystem& AudioSystem::get() {
    static AudioSystem instance; // outlives the Scene, so component dtors are safe
    return instance;
}

void AudioSystem::start() {
    if (running_) return;
    ma_engine_init(nullptr, &engine_);
    ma_engine_listener_set_world_up(&engine_, 0, 0.0f, 1.0f, 0.0f);
    running_ = true;
}

void AudioSystem::stop() {
    if (!running_) return;
    for (auto& s : sounds_) ma_sound_uninit(s.get());
    sounds_.clear();
    ma_engine_uninit(&engine_);
    running_ = false;
}

void AudioSystem::setListener(glm::vec3 position, glm::vec3 forward) {
    if (!running_) return;
    ma_engine_listener_set_position(&engine_, 0, position.x, position.y, position.z);
    ma_engine_listener_set_direction(&engine_, 0, forward.x, forward.y, forward.z);
}

ma_sound* AudioSystem::createSound(const std::string& mp3Path) {
    auto sound = std::make_unique<ma_sound>();
    ma_sound_init_from_file(&engine_, resolveAssetPath(mp3Path).c_str(),
                            MA_SOUND_FLAG_DECODE, nullptr, nullptr, sound.get());
    ma_sound_set_positioning(sound.get(), ma_positioning_absolute);
    ma_sound_set_attenuation_model(sound.get(), ma_attenuation_model_inverse);
    ma_sound_set_min_distance(sound.get(), 1.5f);
    ma_sound_set_max_distance(sound.get(), 40.0f);
    ma_sound_set_rolloff(sound.get(), 1.4f);
    return sounds_.emplace_back(std::move(sound)).get();
}

void AudioSystem::destroySound(ma_sound* sound) {
    if (!running_) return;
    for (auto it = sounds_.begin(); it != sounds_.end(); ++it) {
        if (it->get() != sound) continue;
        ma_sound_uninit(sound);
        sounds_.erase(it);
        return;
    }
}

// ------------------------------------------------------- AudioSourceComponent

AudioSourceComponent::~AudioSourceComponent() {
    for (auto& [name, sound] : clips_) AudioSystem::get().destroySound(sound);
}

void AudioSourceComponent::load(const std::string& name, const std::string& mp3Path) {
    clips_[name] = AudioSystem::get().createSound(mp3Path);
}

void AudioSourceComponent::play(const std::string& name, bool loop, float volume) {
    ma_sound* s = clips_[name];
    ma_sound_set_looping(s, loop ? MA_TRUE : MA_FALSE);
    ma_sound_set_volume(s, volume);
    ma_sound_seek_to_pcm_frame(s, 0);
    ma_sound_start(s);
}

void AudioSourceComponent::stop(const std::string& name) {
    ma_sound_stop(clips_[name]);
}

void AudioSourceComponent::stopAll() {
    for (auto& [name, sound] : clips_) ma_sound_stop(sound);
}

bool AudioSourceComponent::playing(const std::string& name) const {
    return ma_sound_is_playing(clips_.at(name)) == MA_TRUE;
}

void AudioSourceComponent::setPosition(glm::vec3 position) {
    for (auto& [name, sound] : clips_)
        ma_sound_set_position(sound, position.x, position.y, position.z);
}

} // namespace bk
