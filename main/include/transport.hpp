#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "patterns.hpp"

namespace tab5drum {

constexpr uint16_t kMinBpm = 40;
constexpr uint16_t kMaxBpm = 240;
constexpr uint8_t kMinSwing = 50;
constexpr uint8_t kMaxSwing = 75;
constexpr std::size_t kPageCount = 4;
constexpr std::size_t kBassStyleCount = 20;
constexpr std::size_t kBassParamCount = 13;
constexpr std::size_t kDrumParamCount = 5;
constexpr std::size_t kMixEqBusCount = 3;
constexpr std::size_t kMixEqBandCount = 3;

enum class UiPage : uint8_t {
    Mix = 0,
    Drum,
    Bass,
    DrumSynth,
};

enum class MixEqBus : uint8_t {
    Master = 0,
    Drum,
    Bass,
};

enum class MixEqBand : uint8_t {
    Low = 0,
    Mid,
    High,
};

enum class BassWave : uint8_t {
    Saw = 0,
    Square,
    Acid,
    Digi,
};

enum class BassParam : uint8_t {
    Wave = 0,
    Cutoff,
    Resonance,
    Decay,
    Drive,
    Sub,
    Glide,
    Env,
    FxSelect,
    FxAmount,
    Attack,
    Sustain,
    Release,
};

enum class DrumParam : uint8_t {
    Pitch = 0,
    Decay,
    Tone,
    Drive,
    Level,
};

struct BassStep {
    uint8_t note = 36;
    bool gate = false;
    bool accent = false;
    bool slide = false;
};

struct BassParams {
    uint8_t wave = static_cast<uint8_t>(BassWave::Saw);
    uint8_t cutoff = 56;
    uint8_t resonance = 34;
    uint8_t decay = 56;
    uint8_t drive = 46;
    uint8_t sub = 56;
    uint8_t glide = 40;
    uint8_t env = 58;
    uint8_t fx_select = 0;
    uint8_t fx_amount = 28;
    uint8_t attack = 8;
    uint8_t sustain = 34;
    uint8_t release = 24;
};

struct DrumVoiceParams {
    uint8_t pitch = 50;
    uint8_t decay = 50;
    uint8_t tone = 50;
    uint8_t drive = 40;
    uint8_t level = 82;
};

struct DrumEvent {
    uint64_t sample_time = 0;
    DrumVoice voice = DrumVoice::Kick;
    float velocity = 1.0f;
    bool accent = false;
};

struct TransportState {
    bool playing = false;
    uint16_t bpm = 120;
    uint8_t swing = 50;
    std::size_t current_pattern = 0;
    std::size_t current_step = 0;
    uint8_t volume = 80;
    uint8_t drum_volume = 78;
    bool bass_enabled = true;
    uint8_t bass_volume = 34;
    uint8_t master_eq_mask = 0;
    uint8_t drum_eq_mask = 0;
    uint8_t bass_eq_mask = 0;
    uint8_t kit = 0;
    UiPage ui_page = UiPage::Mix;
    uint8_t drum_page = 0;
    bool edit_mode = false;
    bool fill_armed = false;
    uint8_t lane_mute_mask = 0;
    uint8_t bass_style = 0;
    uint8_t bass_root = 0;
    bool arp_enabled = false;
    bool bass_generation_enabled = true;
    bool bass_queued = false;
    bool imu_ready = false;
    uint8_t motion_energy = 0;
    uint8_t motion_direction = 255;
    uint16_t motion_period_ms = 0;
    uint32_t motion_generation = 0;
    uint8_t motion_capture_bars = 0;
    bool motion_capture_active = false;
    BassParams bass_params;
    std::array<DrumVoiceParams, kVoiceCount> drum_voice_params = {};
    DrumVoice selected_drum_voice = DrumVoice::Kick;
};

uint16_t clamp_bpm(int bpm);
uint8_t clamp_swing(int swing);
uint32_t samples_for_step(uint32_t sample_rate, uint16_t bpm, uint8_t swing, std::size_t step_index);

class TapTempo {
public:
    void reset();
    bool tap(uint64_t now_ms, uint16_t* out_bpm);

private:
    static constexpr std::size_t kTapCount = 4;
    uint64_t taps_[kTapCount] = {};
    std::size_t count_ = 0;
    std::size_t next_ = 0;
};

}  // namespace tab5drum
