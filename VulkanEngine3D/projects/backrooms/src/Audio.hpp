#pragma once

// Procedurally synthesized Level 0 ambience (no audio assets): a low brown-
// noise rumble, a 120 Hz fluorescent hum whose loudness follows the nearest
// live light fixture, and sporadic distant thumps from elsewhere in the maze.

namespace bk {

class Audio {
public:
    Audio() = default;
    ~Audio();

    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;

    // Opens the default playback device. Returns false (and the game stays
    // silent) if no audio device is available.
    bool start();
    void stop();

    // 0..1: how loud the fluorescent hum is right now (set from the game
    // thread each frame, based on distance to the nearest live panel).
    void setHumLevel(float level);

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace bk
