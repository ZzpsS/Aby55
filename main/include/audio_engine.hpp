#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "patterns.hpp"
#include "transport.hpp"

namespace tab5drum {

enum class ControlCurve : uint8_t {
    Linear = 0,
    Log,
    Exp,
    Cube,
};

class ControlParam {
public:
    void configure(float min_value, float max_value, ControlCurve curve, float smooth_ms, uint32_t sample_rate,
                   float initial_ui);
    void set_target(float ui_value);
    float process();
    float current() const;

private:
    float map(float ui_value) const;

    std::atomic<float> target_{0.0f};
    float last_target_ = -1000.0f;
    float target_mapped_ = 0.0f;
    float current_ = 0.0f;
    float min_ = 0.0f;
    float max_ = 1.0f;
    float smoothing_ = 1.0f;
    ControlCurve curve_ = ControlCurve::Linear;
};

enum class EnvelopeStage : uint8_t {
    Idle = 0,
    Attack,
    Decay,
    Sustain,
    Release,
};

class BassAdsr {
public:
    void configure(uint32_t sample_rate);
    void set(float attack_s, float decay_s, float sustain, float release_s);
    void trigger_gate(bool on, bool retrigger = true, float level = 1.0f);
    float process();
    bool is_idle() const;
    EnvelopeStage stage() const;

private:
    uint32_t sample_rate_ = 44100;
    EnvelopeStage stage_ = EnvelopeStage::Idle;
    float value_ = 0.0f;
    float attack_s_ = 0.006f;
    float decay_s_ = 0.16f;
    float sustain_ = 0.32f;
    float release_s_ = 0.08f;
    float level_ = 1.0f;
    float attack_inc_ = 0.01f;
    float decay_step_ = 0.001f;
    float release_step_ = 0.001f;
};

class DcBlocker {
public:
    float process(float sample);
    void reset();

private:
    float x1_ = 0.0f;
    float y1_ = 0.0f;
};

class SoftLimiter {
public:
    float process(float sample);
    uint32_t clip_count() const;
    uint8_t peak_percent() const;
    uint8_t gain_reduction_percent() const;
    void reset_stats();

private:
    uint32_t clip_count_ = 0;
    float peak_ = 0.0f;
    float gain_reduction_ = 0.0f;
};

class DriveStage {
public:
    float process(float sample, float drive);
};

class AudioEngine {
public:
    explicit AudioEngine(uint32_t sample_rate);

    void set_pattern_catalog(PatternCatalog* catalog);
    void set_pattern_index(std::size_t index);
    void set_bpm(uint16_t bpm);
    void adjust_bpm(int delta);
    void set_swing(uint8_t swing);
    void adjust_swing(int delta);
    void set_volume(uint8_t volume);
    void adjust_volume(int delta);
    void set_output_profile(OutputProfile profile);
    void set_drum_volume(uint8_t volume);
    void adjust_drum_volume(int delta);
    void set_bass_volume(uint8_t volume);
    void adjust_bass_volume(int delta);
    void toggle_mix_eq(MixEqBus bus, MixEqBand band);
    void set_bass_enabled(bool enabled);
    void toggle_bass_enabled();
    void toggle_bass_generation();
    void toggle_arp();
    void set_kit(uint8_t kit);
    void adjust_kit(int delta);
    void set_ui_page(UiPage page);
    void set_drum_page(uint8_t page);
    void toggle_edit_mode();
    void toggle_lane_mute(DrumVoice voice);
    void toggle_drum_cell(DrumVoice voice, std::size_t step);
    void trigger_fill();
    void adjust_bass_style(int delta);
    void adjust_bass_root(int delta);
    void generate_bassline(uint8_t energy);
    void generate_bassline_from_motion(uint8_t energy, uint8_t direction, uint16_t period_ms);
    void observe_motion(float ax, float ay, float az, float gx, float gy, float gz, uint64_t now_ms);
    void adjust_bass_param(BassParam param, int delta);
    void set_bass_param(BassParam param, uint8_t value);
    void set_bass_fx(uint8_t fx_select, uint8_t amount);
    void finish_motion_capture();
    void set_selected_drum_voice(DrumVoice voice);
    void adjust_drum_param(DrumParam param, int delta);
    void set_drum_param(DrumParam param, uint8_t value);
    void set_playing(bool playing);
    void tap_tempo(uint64_t now_ms);
    void record_audio_timing(uint32_t render_us, uint32_t write_us, uint32_t budget_us);

    TransportState state() const;
    BassStep bass_step(std::size_t step) const;
    const char* bass_style_name(uint8_t style) const;
    const char* bass_root_name(uint8_t root) const;
    std::size_t render_stereo_i16(int16_t* out, std::size_t frames);

private:
    static constexpr uint8_t kMaxRenderedVoices = 18;
    static constexpr uint8_t kLoadShedBlocks = 48;

    struct ActiveVoice {
        DrumVoice voice = DrumVoice::Kick;
        float velocity = 0.0f;
        float phase = 0.0f;
        uint32_t age = 0;
        uint32_t length = 0;
        uint32_t seed = 1;
        DrumVoiceParams params = {};
        bool active = false;
    };

    struct BassSynth {
        float phase = 0.0f;
        float current_freq = 55.0f;
        float target_freq = 55.0f;
        float amp_env = 0.0f;
        float filter = 0.0f;
        float dc_x = 0.0f;
        float dc_y = 0.0f;
        uint32_t age = 0;
        uint32_t gate_samples_left = 0;
        uint32_t seed = 9;
        bool gate = false;
        bool slide = false;
        bool accent = false;
        std::array<float, 2048> fx_delay = {};
        std::size_t fx_write = 0;
    };

    void trigger_step(std::size_t step, uint64_t sample_time);
    void trigger_voice(DrumVoice voice, float velocity);
    void trigger_bass_step(std::size_t step);
    float render_voice(ActiveVoice& voice);
    float render_bass();
    float apply_speaker_low_trim(float sample);
    float control_param_value(BassParam param);
    void refresh_bass_control_targets();
    float apply_mix_eq(float sample, MixEqBus bus, uint8_t mask);
    const Pattern* current_pattern() const;
    uint32_t next_step_samples(std::size_t step) const;
    void write_generated_bass(uint8_t style, uint8_t root, uint32_t seed, uint8_t energy, bool queued,
                              uint8_t direction = 255, uint16_t period_ms = 0);
    void write_generated_bass_variant(uint8_t style, uint8_t root, uint32_t seed, uint8_t energy, bool queued,
                                      uint8_t direction = 255, uint16_t period_ms = 0,
                                      uint8_t preserve_hint = 62);
    void apply_queued_bass_if_needed(std::size_t step);
    void advance_motion_capture();

    uint32_t sample_rate_ = 44100;
    PatternCatalog* catalog_ = nullptr;
    std::atomic<bool> playing_{false};
    std::atomic<uint16_t> bpm_{120};
    std::atomic<uint8_t> swing_{50};
    std::atomic<uint8_t> volume_{80};
    std::atomic<uint8_t> output_profile_{static_cast<uint8_t>(OutputProfile::Speaker)};
    std::atomic<uint8_t> drum_volume_{78};
    std::atomic<uint8_t> bass_volume_{34};
    std::atomic<uint8_t> master_eq_mask_{0};
    std::atomic<uint8_t> drum_eq_mask_{0};
    std::atomic<uint8_t> bass_eq_mask_{0};
    std::atomic<bool> bass_enabled_{true};
    std::atomic<bool> bass_generation_enabled_{true};
    std::atomic<bool> arp_enabled_{false};
    std::atomic<uint8_t> kit_{0};
    std::atomic<uint8_t> ui_page_{static_cast<uint8_t>(UiPage::Mix)};
    std::atomic<uint8_t> drum_page_{0};
    std::atomic<bool> edit_mode_{false};
    std::atomic<bool> fill_armed_{false};
    std::atomic<uint8_t> lane_mute_mask_{0};
    std::atomic<uint8_t> bass_style_{0};
    std::atomic<uint8_t> bass_root_{0};
    std::atomic<bool> bass_queued_{false};
    std::atomic<bool> imu_ready_{false};
    std::atomic<uint8_t> motion_energy_{0};
    std::atomic<uint8_t> motion_direction_{255};
    std::atomic<uint16_t> motion_period_ms_{0};
    std::atomic<uint32_t> motion_generation_{0};
    std::atomic<bool> motion_capture_active_{false};
    std::atomic<uint8_t> motion_capture_bars_{0};
    std::atomic<uint8_t> selected_drum_voice_{0};
    std::atomic<std::size_t> pattern_index_{0};
    std::atomic<std::size_t> current_step_{0};
    uint64_t sample_clock_ = 0;
    uint32_t samples_until_step_ = 0;
    uint32_t bass_seed_ = 0x31415926u;
    uint64_t last_motion_peak_ms_ = 0;
    uint64_t last_motion_generate_ms_ = 0;
    uint16_t motion_period_avg_ms_ = 0;
    bool motion_peak_active_ = false;
    static constexpr uint8_t kMotionCaptureBars = 2;
    static constexpr uint16_t kMotionCooldownSteps = 16;
    std::atomic<uint16_t> motion_capture_steps_{0};
    std::atomic<uint16_t> motion_capture_cooldown_steps_{0};
    std::atomic<uint8_t> motion_capture_peak_count_{0};
    std::atomic<uint8_t> motion_capture_energy_max_{0};
    std::atomic<uint16_t> motion_capture_period_sum_{0};
    std::atomic<uint8_t> motion_capture_period_count_{0};
    std::atomic<uint32_t> motion_capture_seed_mix_{0};
    std::array<std::atomic<uint8_t>, 6> motion_direction_counts_ = {};
    uint8_t fill_steps_left_ = 0;
    TapTempo tap_tempo_;
    std::array<ActiveVoice, 32> voices_ = {};
    BassSynth bass_;
    std::array<ControlParam, kBassParamCount> bass_control_params_ = {};
    BassAdsr bass_adsr_;
    DcBlocker bass_dc_blocker_;
    DcBlocker drum_bus_dc_blocker_;
    DcBlocker master_dc_blocker_;
    SoftLimiter bass_limiter_;
    SoftLimiter drum_limiter_;
    SoftLimiter master_limiter_;
    DriveStage drive_stage_;
    float speaker_bass_trim_ = 0.0f;
    float speaker_low_trim_ = 0.0f;
    std::atomic<uint32_t> audio_clip_count_{0};
    std::atomic<uint8_t> audio_peak_percent_{0};
    std::atomic<uint8_t> limiter_gain_reduction_percent_{0};
    std::atomic<uint8_t> bass_env_stage_{static_cast<uint8_t>(EnvelopeStage::Idle)};
    std::atomic<uint32_t> audio_overrun_count_{0};
    std::atomic<uint32_t> audio_block_peak_us_{0};
    std::atomic<uint8_t> audio_load_shed_blocks_{0};
    std::array<float, kMixEqBusCount> mix_eq_low_ = {};
    std::array<float, kMixEqBusCount> mix_eq_mid_ = {};
    std::array<float, kMixEqBusCount> mix_eq_high_ = {};
    std::array<std::atomic<uint8_t>, kBassParamCount> bass_params_ = {};
    std::array<std::array<std::atomic<uint8_t>, kDrumParamCount>, kVoiceCount> drum_params_ = {};
    std::array<std::atomic<uint8_t>, kMaxSteps> bass_note_ = {};
    std::array<std::atomic<uint8_t>, kMaxSteps> bass_gate_ = {};
    std::array<std::atomic<uint8_t>, kMaxSteps> bass_accent_ = {};
    std::array<std::atomic<uint8_t>, kMaxSteps> bass_slide_ = {};
    std::array<std::atomic<uint8_t>, kMaxSteps> queued_bass_note_ = {};
    std::array<std::atomic<uint8_t>, kMaxSteps> queued_bass_gate_ = {};
    std::array<std::atomic<uint8_t>, kMaxSteps> queued_bass_accent_ = {};
    std::array<std::atomic<uint8_t>, kMaxSteps> queued_bass_slide_ = {};
};

}  // namespace tab5drum
