#pragma once

// Sample playback for Level 0: a thin wrapper around ma_engine (miniaudio's
// high-level mixer, mp3 decoding enabled) plus AudioSourceComponent, an ECS
// component that plays named .mp3 clips spatialized at its entity's position.
//
// The synthesized ambience (Audio.hpp) keeps its own raw ma_device; both share
// the single miniaudio implementation compiled in AudioSystem.cpp.
//
// AudioSystem is a process-lifetime singleton (AudioSystem::get()) so that
// components destroyed during Scene teardown can always release their sounds.

#include <vke/vke.hpp>

#include <miniaudio.h>

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace bk {

class AudioSystem {
public:
    static AudioSystem& get();

    void start(); // opens the default device via ma_engine
    void stop();  // releases every live sound, then the engine

    // Call once per frame from the game loop with the camera pose.
    void setListener(glm::vec3 position, glm::vec3 forward);

    // Creates a 3D-spatialized sound from an .mp3 (decoded up front). Owned by
    // the system; release with destroySound.
    ma_sound* createSound(const std::string& mp3Path);
    void destroySound(ma_sound* sound);

    ~AudioSystem() { stop(); }

private:
    AudioSystem() = default;

    ma_engine engine_{};
    bool running_ = false;
    std::vector<std::unique_ptr<ma_sound>> sounds_;
};

// ECS component: a positional .mp3 emitter. Load named clips once, then drive
// them from game logic; call setPosition every frame so the 3D panning and
// attenuation follow the entity.
struct AudioSourceComponent : vke::Component {
    ~AudioSourceComponent() override;

    void load(const std::string& name, const std::string& mp3Path); // path like a loadModel path
    void play(const std::string& name, bool loop, float volume = 1.0f);
    void stop(const std::string& name);
    void stopAll();
    bool playing(const std::string& name) const;
    void setPosition(glm::vec3 position);

private:
    std::unordered_map<std::string, ma_sound*> clips_;
};

} // namespace bk
