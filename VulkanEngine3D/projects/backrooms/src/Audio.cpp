#include "Audio.hpp"

// The miniaudio implementation lives in AudioSystem.cpp (full build with mp3
// decoding); this TU only needs the declarations for the raw device API.
#include <miniaudio.h>

#include <atomic>
#include <cmath>
#include <cstdint>

namespace bk {

namespace {
constexpr float  kSampleRate = 48000.0f;
constexpr double kTwoPi      = 6.283185307179586;
// All hum/LFO frequencies divide 600 s evenly, so wrapping t there is seamless.
constexpr double kTimeWrap   = 600.0;
} // namespace

struct Audio::Impl {
    ma_device device{};
    bool deviceReady = false;

    std::atomic<float> humTarget{0.15f};

    // ---- DSP state (audio thread only) ----
    double   t        = 0.0;
    float    hum      = 0.0f;   // smoothed hum amplitude
    float    brown    = 0.0f;   // brown-noise integrator
    uint32_t rng      = 0x6D5972A3u;
    float    thumpEnv = 0.0f;
    double   thumpPhase = 0.0;
    float    thumpFreq  = 55.0f;
    float    thumpPan   = 0.5f;
    double   nextThump  = 6.0 * kSampleRate; // first thump after ~6 s

    float frand() { // [-1, 1)
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        return static_cast<int32_t>(rng) / 2147483648.0f;
    }
    float frand01() { return frand() * 0.5f + 0.5f; }

    static void dataCallback(ma_device* dev, void* out, const void*, ma_uint32 frames) {
        static_cast<Impl*>(dev->pUserData)->render(static_cast<float*>(out), frames);
    }

    void render(float* out, ma_uint32 frames) {
        const float target = humTarget.load(std::memory_order_relaxed);
        for (ma_uint32 i = 0; i < frames; ++i) {
            hum += (target - hum) * 0.00008f; // ~100 ms smoothing

            // Low rumble: integrated white noise (1/f^2-ish), slowly breathing.
            float w = frand();
            brown = (brown + w * 0.015f) * 0.998f;
            float breathe = 0.7f + 0.3f * static_cast<float>(std::sin(t * kTwoPi * 0.05));
            float rumble = brown * 0.9f * breathe;

            // Fluorescent hum: 120 Hz + harmonics with a touch of crackle.
            double ph = t * kTwoPi;
            float humSig = static_cast<float>(std::sin(ph * 120.0) * 0.55 +
                                              std::sin(ph * 240.0) * 0.22 +
                                              std::sin(ph * 360.0) * 0.08);
            float crackle = frand();
            humSig *= 1.0f + crackle * crackle * crackle * 0.4f;
            humSig *= hum * 0.30f;

            // Sporadic distant thumps: a decaying low sine, randomly panned.
            if (--nextThump <= 0.0) {
                thumpEnv   = 1.0f;
                thumpFreq  = 42.0f + frand01() * 26.0f;
                thumpPhase = 0.0;
                thumpPan   = 0.2f + frand01() * 0.6f;
                nextThump  = (10.0 + frand01() * 30.0) * kSampleRate;
            }
            thumpPhase += thumpFreq / kSampleRate;
            thumpEnv   *= 0.99993f; // ~0.3 s decay
            float thump = static_cast<float>(std::sin(thumpPhase * kTwoPi)) *
                          thumpEnv * thumpEnv * 0.5f;

            float mono = rumble * 0.35f + humSig;
            float l = mono + thump * (1.0f - thumpPan);
            float r = mono + thump * thumpPan;
            out[i * 2 + 0] = std::tanh(l * 1.4f) * 0.8f;
            out[i * 2 + 1] = std::tanh(r * 1.4f) * 0.8f;

            t += 1.0 / kSampleRate;
            if (t >= kTimeWrap) t -= kTimeWrap;
        }
    }
};

Audio::~Audio() { stop(); }

bool Audio::start() {
    if (impl_) return true;
    impl_ = new Impl();

    ma_device_config cfg   = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format    = ma_format_f32;
    cfg.playback.channels  = 2;
    cfg.sampleRate         = static_cast<ma_uint32>(kSampleRate);
    cfg.dataCallback       = Impl::dataCallback;
    cfg.pUserData          = impl_;

    if (ma_device_init(nullptr, &cfg, &impl_->device) != MA_SUCCESS) {
        delete impl_;
        impl_ = nullptr;
        return false;
    }
    impl_->deviceReady = true;

    if (ma_device_start(&impl_->device) != MA_SUCCESS) {
        stop();
        return false;
    }
    return true;
}

void Audio::stop() {
    if (!impl_) return;
    if (impl_->deviceReady) ma_device_uninit(&impl_->device);
    delete impl_;
    impl_ = nullptr;
}

void Audio::setHumLevel(float level) {
    if (impl_) impl_->humTarget.store(level, std::memory_order_relaxed);
}

} // namespace bk
