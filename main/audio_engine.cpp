#include "audio_engine.hpp"

#include <algorithm>
#include <cmath>

namespace tab5drum {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = kPi * 2.0f;
constexpr float kBassOutputGain = 1.12f;
constexpr float kBassFxDriveGain = 13.5f;
constexpr float kBassGateFraction = 0.55f;
constexpr float kReleaseFastRate = 0.965f;
constexpr float kReleaseSlowRate = 0.99955f;
constexpr std::size_t kRootCount = 12;

const char* const kBassStyleNames[kBassStyleCount] = {
    "Industrial Pulse", "Hardcore Drive", "Gabber Tail", "Acid Grind", "EBM March",
    "Dark Electro", "Noise Techno", "Schranz Roll", "Warehouse Rumble", "Broken Factory",
    "303 Stab", "KickBass Follow", "Minor Rave", "Stutter Bass", "Offbeat Acid",
    "Half-Time Doom", "Syncopated Metal", "DNB Reese Lite", "Reggaeton Dark", "Minimal Punish",
};

const char* const kRootNames[kRootCount] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

struct BassStyleSpec {
    uint8_t density;
    uint8_t accent_rate;
    uint8_t slide_rate;
    int8_t octave_bias;
    uint8_t rhythm_bias;
};

const BassStyleSpec kBassStyleSpecs[kBassStyleCount] = {
    {48, 22, 8, -1, 0},  {66, 32, 10, 0, 1}, {42, 28, 18, -1, 0}, {55, 36, 34, 0, 2},
    {50, 18, 5, -1, 3},  {58, 26, 14, 0, 4}, {72, 24, 16, 0, 5},  {70, 30, 18, 1, 5},
    {44, 34, 6, -1, 1},  {52, 26, 22, 0, 6}, {48, 38, 42, 0, 2},  {62, 28, 7, -1, 7},
    {56, 26, 18, 1, 0},  {78, 18, 10, 0, 8}, {50, 34, 36, 0, 9},  {34, 30, 8, -1, 10},
    {64, 32, 20, 0, 6},  {74, 20, 18, 1, 11}, {58, 22, 12, -1, 12}, {38, 14, 4, -1, 13},
};

const int kMinorScale[7] = {0, 2, 3, 5, 7, 8, 10};

float clamp_unit(float value)
{
    return std::clamp(value, -1.0f, 1.0f);
}

uint8_t clamp_u8(int value, int lo = 0, int hi = 100)
{
    return static_cast<uint8_t>(std::clamp(value, lo, hi));
}

uint8_t bass_param_max(BassParam param)
{
    switch (param) {
    case BassParam::Wave:
    case BassParam::FxSelect:
        return 3;
    default:
        return 100;
    }
}

uint32_t next_rng(uint32_t& seed)
{
    seed = seed * 1664525u + 1013904223u;
    return seed;
}

float next_noise(uint32_t& seed)
{
    const auto value = static_cast<int32_t>((next_rng(seed) >> 9) & 0x7FFFFFu);
    return (static_cast<float>(value) / 4194304.0f) - 1.0f;
}

uint8_t dominant_motion_direction(float gx, float gy, float gz)
{
    const float ax = std::fabs(gx);
    const float ay = std::fabs(gy);
    const float az = std::fabs(gz);
    if (ax >= ay && ax >= az) {
        return gx >= 0.0f ? 0 : 1;
    }
    if (ay >= ax && ay >= az) {
        return gy >= 0.0f ? 2 : 3;
    }
    return gz >= 0.0f ? 4 : 5;
}

uint8_t motion_energy_bucket(float gyro_mag, float accel_delta)
{
    const float energy = gyro_mag * 0.0045f + accel_delta * 22.0f;
    if (energy > 5.2f) {
        return 2;
    }
    if (energy > 2.6f) {
        return 1;
    }
    return 0;
}

float midi_to_freq(uint8_t note)
{
    return 440.0f * std::pow(2.0f, (static_cast<float>(note) - 69.0f) / 12.0f);
}

uint8_t strongest_direction(const std::array<std::atomic<uint8_t>, 6>& counts)
{
    uint8_t best = 255;
    uint8_t best_count = 0;
    for (std::size_t i = 0; i < counts.size(); ++i) {
        const uint8_t count = counts[i].load();
        if (count > best_count) {
            best_count = count;
            best = static_cast<uint8_t>(i);
        }
    }
    return best_count == 0 ? 255 : best;
}

uint32_t voice_length(uint32_t sample_rate, DrumVoice voice, const DrumVoiceParams& params)
{
    const float base_ms = [voice]() {
        switch (voice) {
        case DrumVoice::Kick:
            return 260.0f;
        case DrumVoice::Snare:
            return 190.0f;
        case DrumVoice::Clap:
            return 240.0f;
        case DrumVoice::ClosedHat:
            return 75.0f;
        case DrumVoice::OpenHat:
            return 360.0f;
        case DrumVoice::Perc:
            return 170.0f;
        default:
            return 100.0f;
        }
    }();
    const float scale = 0.45f + (static_cast<float>(params.decay) / 50.0f);
    const float ms = std::clamp(base_ms * scale, 28.0f, 900.0f);
    return std::max<uint32_t>(1, static_cast<uint32_t>((static_cast<float>(sample_rate) * ms) / 1000.0f));
}

}  // namespace

AudioEngine::AudioEngine(uint32_t sample_rate) : sample_rate_(sample_rate)
{
    bass_params_[static_cast<std::size_t>(BassParam::Wave)].store(static_cast<uint8_t>(BassWave::Saw));
    bass_params_[static_cast<std::size_t>(BassParam::Cutoff)].store(56);
    bass_params_[static_cast<std::size_t>(BassParam::Resonance)].store(34);
    bass_params_[static_cast<std::size_t>(BassParam::Decay)].store(56);
    bass_params_[static_cast<std::size_t>(BassParam::Drive)].store(46);
    bass_params_[static_cast<std::size_t>(BassParam::Sub)].store(56);
    bass_params_[static_cast<std::size_t>(BassParam::Glide)].store(40);
    bass_params_[static_cast<std::size_t>(BassParam::Env)].store(58);
    bass_params_[static_cast<std::size_t>(BassParam::FxSelect)].store(0);
    bass_params_[static_cast<std::size_t>(BassParam::FxAmount)].store(28);
    bass_params_[static_cast<std::size_t>(BassParam::Attack)].store(8);
    bass_params_[static_cast<std::size_t>(BassParam::Sustain)].store(34);
    bass_params_[static_cast<std::size_t>(BassParam::Release)].store(24);

    for (std::size_t voice = 0; voice < kVoiceCount; ++voice) {
        drum_params_[voice][static_cast<std::size_t>(DrumParam::Pitch)].store(50);
        drum_params_[voice][static_cast<std::size_t>(DrumParam::Decay)].store(50);
        drum_params_[voice][static_cast<std::size_t>(DrumParam::Tone)].store(50);
        drum_params_[voice][static_cast<std::size_t>(DrumParam::Drive)].store(40);
        drum_params_[voice][static_cast<std::size_t>(DrumParam::Level)].store(82);
    }
    write_generated_bass(0, 0, bass_seed_, 1, false);
}

void AudioEngine::set_pattern_catalog(PatternCatalog* catalog)
{
    catalog_ = catalog;
    set_pattern_index(0);
}

void AudioEngine::set_pattern_index(std::size_t index)
{
    pattern_index_.store(index);
    current_step_.store(0);
    samples_until_step_ = 0;

    if (const auto* pattern = current_pattern()) {
        swing_.store(clamp_swing(pattern->default_swing));
    }
    generate_bassline(1);
}

void AudioEngine::set_bpm(uint16_t bpm)
{
    bpm_.store(clamp_bpm(bpm));
}

void AudioEngine::adjust_bpm(int delta)
{
    set_bpm(static_cast<uint16_t>(static_cast<int>(bpm_.load()) + delta));
}

void AudioEngine::set_swing(uint8_t swing)
{
    swing_.store(clamp_swing(swing));
}

void AudioEngine::adjust_swing(int delta)
{
    set_swing(static_cast<uint8_t>(static_cast<int>(swing_.load()) + delta));
}

void AudioEngine::set_volume(uint8_t volume)
{
    volume_.store(std::min<uint8_t>(volume, 100));
}

void AudioEngine::adjust_volume(int delta)
{
    set_volume(clamp_u8(static_cast<int>(volume_.load()) + delta));
}

void AudioEngine::set_drum_volume(uint8_t volume)
{
    drum_volume_.store(std::min<uint8_t>(volume, 100));
}

void AudioEngine::adjust_drum_volume(int delta)
{
    set_drum_volume(clamp_u8(static_cast<int>(drum_volume_.load()) + delta));
}

void AudioEngine::set_bass_volume(uint8_t volume)
{
    bass_volume_.store(std::min<uint8_t>(volume, 100));
}

void AudioEngine::adjust_bass_volume(int delta)
{
    set_bass_volume(clamp_u8(static_cast<int>(bass_volume_.load()) + delta));
}

void AudioEngine::toggle_mix_eq(MixEqBus bus, MixEqBand band)
{
    const auto bit = static_cast<uint8_t>(1u << static_cast<uint8_t>(band));
    auto* mask = &master_eq_mask_;
    switch (bus) {
    case MixEqBus::Master:
        mask = &master_eq_mask_;
        break;
    case MixEqBus::Drum:
        mask = &drum_eq_mask_;
        break;
    case MixEqBus::Bass:
        mask = &bass_eq_mask_;
        break;
    }
    mask->store(static_cast<uint8_t>(mask->load() ^ bit));
}

void AudioEngine::set_bass_enabled(bool enabled)
{
    bass_enabled_.store(enabled);
}

void AudioEngine::toggle_bass_enabled()
{
    bass_enabled_.store(!bass_enabled_.load());
}

void AudioEngine::toggle_bass_generation()
{
    const bool enabled = !bass_generation_enabled_.load();
    bass_generation_enabled_.store(enabled);
    if (!enabled) {
        motion_capture_active_.store(false);
        motion_capture_bars_.store(0);
        motion_capture_steps_.store(0);
        bass_queued_.store(false);
    }
}

void AudioEngine::toggle_arp()
{
    arp_enabled_.store(!arp_enabled_.load());
}

void AudioEngine::set_kit(uint8_t kit)
{
    kit_.store(static_cast<uint8_t>(kit % 3));
}

void AudioEngine::adjust_kit(int delta)
{
    const int next = (static_cast<int>(kit_.load()) + delta + 3) % 3;
    set_kit(static_cast<uint8_t>(next));
}

void AudioEngine::set_ui_page(UiPage page)
{
    ui_page_.store(static_cast<uint8_t>(page));
}

void AudioEngine::set_drum_page(uint8_t page)
{
    drum_page_.store(page > 0 ? 1 : 0);
}

void AudioEngine::toggle_edit_mode()
{
    edit_mode_.store(!edit_mode_.load());
}

void AudioEngine::toggle_lane_mute(DrumVoice voice)
{
    const uint8_t bit = static_cast<uint8_t>(1u << static_cast<uint8_t>(voice));
    lane_mute_mask_.store(lane_mute_mask_.load() ^ bit);
}

void AudioEngine::toggle_drum_cell(DrumVoice voice, std::size_t step)
{
    if (!edit_mode_.load() || catalog_ == nullptr || catalog_->empty() || step >= kMaxSteps) {
        return;
    }
    auto& pattern = catalog_->mutable_at(pattern_index_.load());
    auto& value = pattern.lanes[static_cast<std::size_t>(voice)][step];
    value = value == 0 ? 1 : (value == 1 ? 2 : 0);
}

void AudioEngine::trigger_fill()
{
    fill_armed_.store(true);
}

void AudioEngine::adjust_bass_style(int delta)
{
    const int next = (static_cast<int>(bass_style_.load()) + delta + static_cast<int>(kBassStyleCount)) %
                     static_cast<int>(kBassStyleCount);
    bass_style_.store(static_cast<uint8_t>(next));
    generate_bassline(1);
}

void AudioEngine::adjust_bass_root(int delta)
{
    const int next = (static_cast<int>(bass_root_.load()) + delta + static_cast<int>(kRootCount)) %
                     static_cast<int>(kRootCount);
    bass_root_.store(static_cast<uint8_t>(next));
    generate_bassline(1);
}

void AudioEngine::generate_bassline(uint8_t energy)
{
    if (!bass_generation_enabled_.load()) {
        return;
    }
    bass_seed_ = next_rng(bass_seed_) ^ (static_cast<uint32_t>(sample_clock_) + 0x9E3779B9u);
    const bool queued = playing_.load();
    write_generated_bass_variant(bass_style_.load(), bass_root_.load(), bass_seed_, std::min<uint8_t>(energy, 2), queued);
    bass_queued_.store(queued);
    if (!queued) {
        motion_generation_.fetch_add(1);
    }
}

void AudioEngine::generate_bassline_from_motion(uint8_t energy, uint8_t direction, uint16_t period_ms)
{
    if (!bass_generation_enabled_.load()) {
        return;
    }
    bass_seed_ = next_rng(bass_seed_) ^
                 (static_cast<uint32_t>(sample_clock_) + 0x85EBCA6Bu) ^
                  (static_cast<uint32_t>(direction) << 24) ^
                  (static_cast<uint32_t>(period_ms) << 8);
    const uint8_t preserve = energy >= 2 ? 42 : (energy == 1 ? 56 : 70);
    write_generated_bass_variant(bass_style_.load(), bass_root_.load(), bass_seed_, std::min<uint8_t>(energy, 2), true,
                                 direction, period_ms, preserve);
    bass_queued_.store(true);
    motion_energy_.store(std::min<uint8_t>(energy, 2));
    motion_direction_.store(direction);
    motion_period_ms_.store(period_ms);
    motion_generation_.fetch_add(1);
}

void AudioEngine::observe_motion(float ax, float ay, float az, float gx, float gy, float gz, uint64_t now_ms)
{
    imu_ready_.store(true);

    const float gyro_mag = std::sqrt(gx * gx + gy * gy + gz * gz);
    const float accel_mag = std::sqrt(ax * ax + ay * ay + az * az);
    const float accel_delta = std::fabs(accel_mag - 1.0f);
    const uint8_t energy = motion_energy_bucket(gyro_mag, accel_delta);
    const bool above = gyro_mag > 280.0f || accel_delta > 0.55f;
    const bool below = gyro_mag < 170.0f && accel_delta < 0.32f;

    if (below) {
        motion_peak_active_ = false;
    }

    if (!above || motion_peak_active_) {
        return;
    }

    motion_peak_active_ = true;
    const uint8_t direction = dominant_motion_direction(gx, gy, gz);
    uint16_t period_ms = motion_period_avg_ms_;
    if (last_motion_peak_ms_ != 0) {
        const uint64_t raw_period = now_ms - last_motion_peak_ms_;
        if (raw_period >= 120 && raw_period <= 1400) {
            const auto period = static_cast<uint16_t>(raw_period);
            period_ms = motion_period_avg_ms_ == 0 ? period :
                        static_cast<uint16_t>((static_cast<uint32_t>(motion_period_avg_ms_) * 3 + period) / 4);
            motion_period_avg_ms_ = period_ms;
        }
    }
    last_motion_peak_ms_ = now_ms;

    if (period_ms == 0) {
        period_ms = 420;
    }

    motion_energy_.store(energy);
    motion_direction_.store(direction);
    motion_period_ms_.store(period_ms);

    if (!bass_generation_enabled_.load()) {
        motion_capture_active_.store(false);
        return;
    }

    if (!motion_capture_active_.load()) {
        if (!playing_.load() || bass_queued_.load() || motion_capture_cooldown_steps_.load() > 0) {
            return;
        }
        if (now_ms - last_motion_generate_ms_ < 650) {
            return;
        }
        motion_capture_active_.store(true);
        motion_capture_steps_.store(0);
        motion_capture_bars_.store(0);
        motion_capture_peak_count_.store(0);
        motion_capture_energy_max_.store(0);
        motion_capture_period_sum_.store(0);
        motion_capture_period_count_.store(0);
        motion_capture_seed_mix_.store(static_cast<uint32_t>(now_ms) ^ static_cast<uint32_t>(sample_clock_));
        last_motion_generate_ms_ = now_ms;
        for (auto& count : motion_direction_counts_) {
            count.store(0);
        }
    }

    if (!motion_capture_active_.load()) {
        motion_energy_.store(energy);
        motion_direction_.store(direction);
        motion_period_ms_.store(period_ms);
        return;
    }

    const auto old_energy = motion_capture_energy_max_.load();
    if (energy > old_energy) {
        motion_capture_energy_max_.store(energy);
    }
    if (direction < motion_direction_counts_.size()) {
        const uint8_t count = motion_direction_counts_[direction].load();
        motion_direction_counts_[direction].store(count == 255 ? 255 : static_cast<uint8_t>(count + 1));
    }
    if (period_ms > 0) {
        const auto period_count = motion_capture_period_count_.load();
        if (period_count < 16) {
            motion_capture_period_sum_.store(static_cast<uint16_t>(
                motion_capture_period_sum_.load() + std::min<uint16_t>(period_ms, 1400)));
            motion_capture_period_count_.store(static_cast<uint8_t>(period_count + 1));
        }
    }
    const uint8_t peak_count = motion_capture_peak_count_.load();
    motion_capture_peak_count_.store(peak_count == 255 ? 255 : static_cast<uint8_t>(peak_count + 1));
    motion_capture_seed_mix_.store(motion_capture_seed_mix_.load() ^
                                   (static_cast<uint32_t>(direction) << ((motion_capture_peak_count_.load() % 4) * 8)) ^
                                   (static_cast<uint32_t>(period_ms) * 2654435761u));
}

void AudioEngine::adjust_bass_param(BassParam param, int delta)
{
    const auto index = static_cast<std::size_t>(param);
    if (index >= kBassParamCount) {
        return;
    }
    const int max_value = bass_param_max(param);
    const int next = static_cast<int>(bass_params_[index].load()) + delta;
    bass_params_[index].store(clamp_u8(next, 0, max_value));
}

void AudioEngine::set_bass_param(BassParam param, uint8_t value)
{
    const auto index = static_cast<std::size_t>(param);
    if (index >= kBassParamCount) {
        return;
    }
    const int max_value = bass_param_max(param);
    bass_params_[index].store(clamp_u8(value, 0, max_value));
}

void AudioEngine::set_bass_fx(uint8_t fx_select, uint8_t amount)
{
    bass_params_[static_cast<std::size_t>(BassParam::FxSelect)].store(clamp_u8(fx_select, 0, 3));
    bass_params_[static_cast<std::size_t>(BassParam::FxAmount)].store(clamp_u8(amount));
}

void AudioEngine::finish_motion_capture()
{
    if (!motion_capture_active_.exchange(false)) {
        return;
    }
    if (!bass_generation_enabled_.load()) {
        motion_capture_steps_.store(0);
        motion_capture_bars_.store(0);
        motion_capture_cooldown_steps_.store(kMotionCooldownSteps);
        return;
    }

    const uint8_t peak_count = motion_capture_peak_count_.exchange(0);
    const uint8_t energy = std::max<uint8_t>(motion_capture_energy_max_.exchange(0), peak_count >= 6 ? 1 : 0);
    const uint8_t direction = strongest_direction(motion_direction_counts_);
    const uint8_t period_count = motion_capture_period_count_.exchange(0);
    const uint16_t period_sum = motion_capture_period_sum_.exchange(0);
    const uint16_t period_ms = period_count == 0 ? motion_period_avg_ms_ :
                               static_cast<uint16_t>(period_sum / period_count);
    const uint32_t seed_mix = motion_capture_seed_mix_.exchange(0);

    motion_capture_steps_.store(0);
    motion_capture_bars_.store(0);
    motion_capture_cooldown_steps_.store(kMotionCooldownSteps);
    for (auto& count : motion_direction_counts_) {
        count.store(0);
    }

    bass_seed_ ^= seed_mix;
    generate_bassline_from_motion(energy, direction, period_ms == 0 ? 420 : period_ms);
}

void AudioEngine::set_selected_drum_voice(DrumVoice voice)
{
    selected_drum_voice_.store(static_cast<uint8_t>(voice));
}

void AudioEngine::adjust_drum_param(DrumParam param, int delta)
{
    const auto voice = std::min<std::size_t>(selected_drum_voice_.load(), kVoiceCount - 1);
    const auto index = static_cast<std::size_t>(param);
    if (index >= kDrumParamCount) {
        return;
    }
    const int next = static_cast<int>(drum_params_[voice][index].load()) + delta;
    drum_params_[voice][index].store(clamp_u8(next));
}

void AudioEngine::set_drum_param(DrumParam param, uint8_t value)
{
    const auto voice = std::min<std::size_t>(selected_drum_voice_.load(), kVoiceCount - 1);
    const auto index = static_cast<std::size_t>(param);
    if (index >= kDrumParamCount) {
        return;
    }
    drum_params_[voice][index].store(clamp_u8(value));
}

void AudioEngine::set_playing(bool playing)
{
    const bool was_playing = playing_.exchange(playing);
    if (playing && !was_playing) {
        current_step_.store(0);
        samples_until_step_ = 0;
        sample_clock_ = 0;
        fill_steps_left_ = 0;
    }
}

void AudioEngine::tap_tempo(uint64_t now_ms)
{
    uint16_t tapped_bpm = 0;
    if (tap_tempo_.tap(now_ms, &tapped_bpm)) {
        set_bpm(tapped_bpm);
    }
}

TransportState AudioEngine::state() const
{
    TransportState state;
    state.playing = playing_.load();
    state.bpm = bpm_.load();
    state.swing = swing_.load();
    state.current_pattern = pattern_index_.load();
    state.current_step = current_step_.load();
    state.volume = volume_.load();
    state.drum_volume = drum_volume_.load();
    state.bass_enabled = bass_enabled_.load();
    state.bass_volume = bass_volume_.load();
    state.master_eq_mask = master_eq_mask_.load();
    state.drum_eq_mask = drum_eq_mask_.load();
    state.bass_eq_mask = bass_eq_mask_.load();
    state.kit = kit_.load();
    state.ui_page = static_cast<UiPage>(ui_page_.load());
    state.drum_page = drum_page_.load();
    state.edit_mode = edit_mode_.load();
    state.fill_armed = fill_armed_.load();
    state.lane_mute_mask = lane_mute_mask_.load();
    state.bass_style = bass_style_.load();
    state.bass_root = bass_root_.load();
    state.arp_enabled = arp_enabled_.load();
    state.bass_generation_enabled = bass_generation_enabled_.load();
    state.bass_queued = bass_queued_.load();
    state.imu_ready = imu_ready_.load();
    state.motion_energy = motion_energy_.load();
    state.motion_direction = motion_direction_.load();
    state.motion_period_ms = motion_period_ms_.load();
    state.motion_generation = motion_generation_.load();
    state.motion_capture_bars = motion_capture_bars_.load();
    state.motion_capture_active = motion_capture_active_.load();
    state.bass_params.wave = bass_params_[static_cast<std::size_t>(BassParam::Wave)].load();
    state.bass_params.cutoff = bass_params_[static_cast<std::size_t>(BassParam::Cutoff)].load();
    state.bass_params.resonance = bass_params_[static_cast<std::size_t>(BassParam::Resonance)].load();
    state.bass_params.decay = bass_params_[static_cast<std::size_t>(BassParam::Decay)].load();
    state.bass_params.drive = bass_params_[static_cast<std::size_t>(BassParam::Drive)].load();
    state.bass_params.sub = bass_params_[static_cast<std::size_t>(BassParam::Sub)].load();
    state.bass_params.glide = bass_params_[static_cast<std::size_t>(BassParam::Glide)].load();
    state.bass_params.env = bass_params_[static_cast<std::size_t>(BassParam::Env)].load();
    state.bass_params.fx_select = bass_params_[static_cast<std::size_t>(BassParam::FxSelect)].load();
    state.bass_params.fx_amount = bass_params_[static_cast<std::size_t>(BassParam::FxAmount)].load();
    state.bass_params.attack = bass_params_[static_cast<std::size_t>(BassParam::Attack)].load();
    state.bass_params.sustain = bass_params_[static_cast<std::size_t>(BassParam::Sustain)].load();
    state.bass_params.release = bass_params_[static_cast<std::size_t>(BassParam::Release)].load();
    for (std::size_t voice = 0; voice < kVoiceCount; ++voice) {
        state.drum_voice_params[voice].pitch = drum_params_[voice][static_cast<std::size_t>(DrumParam::Pitch)].load();
        state.drum_voice_params[voice].decay = drum_params_[voice][static_cast<std::size_t>(DrumParam::Decay)].load();
        state.drum_voice_params[voice].tone = drum_params_[voice][static_cast<std::size_t>(DrumParam::Tone)].load();
        state.drum_voice_params[voice].drive = drum_params_[voice][static_cast<std::size_t>(DrumParam::Drive)].load();
        state.drum_voice_params[voice].level = drum_params_[voice][static_cast<std::size_t>(DrumParam::Level)].load();
    }
    state.selected_drum_voice = static_cast<DrumVoice>(selected_drum_voice_.load());
    return state;
}

BassStep AudioEngine::bass_step(std::size_t step) const
{
    const auto index = step % kMaxSteps;
    BassStep result;
    result.note = bass_note_[index].load();
    result.gate = bass_gate_[index].load() != 0;
    result.accent = bass_accent_[index].load() != 0;
    result.slide = bass_slide_[index].load() != 0;
    return result;
}

const char* AudioEngine::bass_style_name(uint8_t style) const
{
    return kBassStyleNames[style % kBassStyleCount];
}

const char* AudioEngine::bass_root_name(uint8_t root) const
{
    return kRootNames[root % kRootCount];
}

std::size_t AudioEngine::render_stereo_i16(int16_t* out, std::size_t frames)
{
    if (out == nullptr) {
        return 0;
    }

    const auto master_volume = static_cast<float>(volume_.load()) / 100.0f;
    const auto drum_volume = static_cast<float>(drum_volume_.load()) / 100.0f;

    for (std::size_t frame = 0; frame < frames; ++frame) {
        const Pattern* pattern = current_pattern();
        if (playing_.load() && pattern != nullptr && samples_until_step_ == 0) {
            const auto step = current_step_.load() % pattern->steps;
            apply_queued_bass_if_needed(step);
            trigger_step(step, sample_clock_);
            trigger_bass_step(step);
            current_step_.store((step + 1) % pattern->steps);
            advance_motion_capture();
            samples_until_step_ = next_step_samples(step);
        }

        float drums = 0.0f;
        for (auto& voice : voices_) {
            if (voice.active) {
                drums += render_voice(voice);
            }
        }
        drums = apply_mix_eq(drums, MixEqBus::Drum, drum_eq_mask_.load());

        float sample = drums * drum_volume;
        if (bass_enabled_.load()) {
            sample += apply_mix_eq(render_bass(), MixEqBus::Bass, bass_eq_mask_.load()) *
                      (static_cast<float>(bass_volume_.load()) / 100.0f);
        }
        sample = apply_mix_eq(sample, MixEqBus::Master, master_eq_mask_.load());

        const auto scaled = clamp_unit(std::tanh(sample * 0.9f) * master_volume);
        const auto pcm = static_cast<int16_t>(scaled * 32767.0f);
        out[frame * 2] = pcm;
        out[frame * 2 + 1] = pcm;

        if (playing_.load() && samples_until_step_ > 0) {
            --samples_until_step_;
        }
        ++sample_clock_;
    }

    return frames;
}

void AudioEngine::trigger_step(std::size_t step, uint64_t sample_time)
{
    (void)sample_time;

    const Pattern* pattern = current_pattern();
    if (pattern == nullptr) {
        return;
    }

    if (step == 0 && fill_armed_.exchange(false)) {
        fill_steps_left_ = 16;
    }

    const uint8_t mute_mask = lane_mute_mask_.load();
    for (std::size_t voice_index = 0; voice_index < kVoiceCount; ++voice_index) {
        if ((mute_mask & (1u << voice_index)) != 0) {
            continue;
        }
        const auto voice = static_cast<DrumVoice>(voice_index);
        const auto hit = pattern->hit(voice, step);
        if (hit != 0) {
            trigger_voice(voice, hit == 2 ? 1.0f : 0.72f);
        }
    }

    if (fill_steps_left_ > 0) {
        const auto fill_pos = (16 - fill_steps_left_) % 16;
        if ((fill_pos % 2) == 0) {
            trigger_voice(DrumVoice::ClosedHat, 0.64f);
        }
        if (fill_pos == 12 || fill_pos == 14) {
            trigger_voice(DrumVoice::Snare, 0.78f);
        }
        if (fill_pos == 15) {
            trigger_voice(DrumVoice::Perc, 0.9f);
        }
        --fill_steps_left_;
    }
}

void AudioEngine::trigger_voice(DrumVoice voice, float velocity)
{
    auto* selected = &voices_[0];
    for (auto& slot : voices_) {
        if (!slot.active) {
            selected = &slot;
            break;
        }
        if (slot.age > selected->age) {
            selected = &slot;
        }
    }

    const auto voice_index = static_cast<std::size_t>(voice);
    DrumVoiceParams params;
    params.pitch = drum_params_[voice_index][static_cast<std::size_t>(DrumParam::Pitch)].load();
    params.decay = drum_params_[voice_index][static_cast<std::size_t>(DrumParam::Decay)].load();
    params.tone = drum_params_[voice_index][static_cast<std::size_t>(DrumParam::Tone)].load();
    params.drive = drum_params_[voice_index][static_cast<std::size_t>(DrumParam::Drive)].load();
    params.level = drum_params_[voice_index][static_cast<std::size_t>(DrumParam::Level)].load();

    if (voice == DrumVoice::ClosedHat) {
        for (auto& slot : voices_) {
            if (slot.active && slot.voice == DrumVoice::OpenHat) {
                slot.active = false;
            }
        }
    }

    selected->voice = voice;
    selected->velocity = velocity;
    selected->phase = 0.0f;
    selected->age = 0;
    selected->length = voice_length(sample_rate_, voice, params);
    selected->seed = static_cast<uint32_t>(sample_clock_ ^ (static_cast<uint32_t>(voice) * 2654435761u));
    selected->params = params;
    selected->active = true;
}

void AudioEngine::trigger_bass_step(std::size_t step)
{
    const auto index = step % kMaxSteps;
    if (bass_gate_[index].load() == 0) {
        bass_.gate = false;
        bass_.gate_samples_left = 0;
        return;
    }

    const auto note = bass_note_[index].load();
    bass_.target_freq = midi_to_freq(note);
    bass_.slide = bass_slide_[index].load() != 0 && bass_.gate;
    bass_.accent = bass_accent_[index].load() != 0;
    if (!bass_.slide) {
        bass_.current_freq = bass_.target_freq;
        bass_.amp_env = bass_.accent ? 1.0f : 0.74f;
        bass_.age = 0;
    } else {
        bass_.amp_env = std::max(bass_.amp_env, bass_.accent ? 0.8f : 0.55f);
    }
    bass_.gate_samples_left = std::max<uint32_t>(1, static_cast<uint32_t>(
                                                     static_cast<float>(next_step_samples(index)) * kBassGateFraction));
    bass_.gate = true;
}

float AudioEngine::render_voice(ActiveVoice& voice)
{
    const float t = static_cast<float>(voice.age) / static_cast<float>(sample_rate_);
    const float progress = static_cast<float>(voice.age) / static_cast<float>(voice.length);
    const float pitch_mul = std::pow(2.0f, (static_cast<float>(voice.params.pitch) - 50.0f) / 72.0f);
    const float tone = static_cast<float>(voice.params.tone) / 100.0f;
    const float drive = 1.0f + static_cast<float>(voice.params.drive) * 0.035f + static_cast<float>(kit_.load()) * 0.28f;
    const float level = static_cast<float>(voice.params.level) / 82.0f;
    float sample = 0.0f;

    switch (voice.voice) {
    case DrumVoice::Kick: {
        const float env = std::exp(-t * (15.0f + tone * 8.0f));
        const float pitch_env = std::exp(-t * 34.0f);
        const float freq = (43.0f + (128.0f * pitch_env)) * pitch_mul;
        voice.phase += kTwoPi * freq / static_cast<float>(sample_rate_);
        sample = std::sin(voice.phase) * env * 1.35f;
        break;
    }
    case DrumVoice::Snare: {
        const float env = std::exp(-t * 22.0f);
        voice.phase += kTwoPi * (155.0f + tone * 130.0f) * pitch_mul / static_cast<float>(sample_rate_);
        sample = ((next_noise(voice.seed) * (0.9f - tone * 0.35f)) + (std::sin(voice.phase) * (0.1f + tone * 0.35f))) * env;
        break;
    }
    case DrumVoice::Clap: {
        const float burst = (progress < 0.12f || (progress > 0.18f && progress < 0.28f) ||
                             (progress > 0.34f && progress < 0.55f))
                                ? 1.0f
                                : 0.25f;
        const float env = std::exp(-t * (11.0f + tone * 10.0f)) * burst;
        sample = next_noise(voice.seed) * env * 0.9f;
        break;
    }
    case DrumVoice::ClosedHat: {
        const float env = std::exp(-t * (52.0f + tone * 32.0f));
        const float metallic = next_noise(voice.seed) - (0.35f * next_noise(voice.seed));
        sample = metallic * env * (0.48f + tone * 0.28f);
        break;
    }
    case DrumVoice::OpenHat: {
        const float env = std::exp(-t * (7.0f + tone * 8.0f));
        const float metallic = next_noise(voice.seed) - (0.25f * next_noise(voice.seed));
        sample = metallic * env * (0.42f + tone * 0.25f);
        break;
    }
    case DrumVoice::Perc: {
        const float env = std::exp(-t * (18.0f + tone * 18.0f));
        const float freq = (210.0f + (210.0f * std::exp(-t * 18.0f))) * pitch_mul;
        voice.phase += kTwoPi * freq / static_cast<float>(sample_rate_);
        sample = (std::sin(voice.phase) * (0.68f + tone * 0.2f) + next_noise(voice.seed) * (0.28f - tone * 0.12f)) * env;
        break;
    }
    default:
        sample = 0.0f;
        break;
    }

    ++voice.age;
    if (voice.age >= voice.length) {
        voice.active = false;
    }

    return std::tanh(sample * drive) * voice.velocity * level;
}

float AudioEngine::render_bass()
{
    if (!bass_.gate && bass_.amp_env < 0.0005f) {
        return 0.0f;
    }

    const float wave = static_cast<float>(bass_params_[static_cast<std::size_t>(BassParam::Wave)].load());
    const float cutoff = static_cast<float>(bass_params_[static_cast<std::size_t>(BassParam::Cutoff)].load()) / 100.0f;
    const float resonance = static_cast<float>(bass_params_[static_cast<std::size_t>(BassParam::Resonance)].load()) / 100.0f;
    const float decay = static_cast<float>(bass_params_[static_cast<std::size_t>(BassParam::Decay)].load()) / 100.0f;
    const float drive = static_cast<float>(bass_params_[static_cast<std::size_t>(BassParam::Drive)].load()) / 100.0f;
    const float sub = static_cast<float>(bass_params_[static_cast<std::size_t>(BassParam::Sub)].load()) / 100.0f;
    const float glide = static_cast<float>(bass_params_[static_cast<std::size_t>(BassParam::Glide)].load()) / 100.0f;
    const float env = static_cast<float>(bass_params_[static_cast<std::size_t>(BassParam::Env)].load()) / 100.0f;
    const uint8_t fx_select = bass_params_[static_cast<std::size_t>(BassParam::FxSelect)].load();
    const float fx_amount = static_cast<float>(bass_params_[static_cast<std::size_t>(BassParam::FxAmount)].load()) / 100.0f;
    const float attack = static_cast<float>(bass_params_[static_cast<std::size_t>(BassParam::Attack)].load()) / 100.0f;
    const float sustain = static_cast<float>(bass_params_[static_cast<std::size_t>(BassParam::Sustain)].load()) / 100.0f;
    const float release = static_cast<float>(bass_params_[static_cast<std::size_t>(BassParam::Release)].load()) / 100.0f;

    if (bass_.gate && bass_.gate_samples_left > 0) {
        --bass_.gate_samples_left;
        if (bass_.gate_samples_left == 0) {
            bass_.gate = false;
        }
    }

    const float glide_rate = std::clamp((bass_.slide ? 0.0018f : 0.18f) * (1.18f - glide * 0.96f), 0.0010f, 0.2f);
    bass_.current_freq += (bass_.target_freq - bass_.current_freq) * glide_rate;
    float render_freq = bass_.current_freq;
    if (arp_enabled_.load() && bass_.gate) {
        const uint32_t period = std::max<uint32_t>(320, static_cast<uint32_t>(
                                                         static_cast<float>(sample_rate_) * (0.028f + (0.065f * (1.0f - env)))));
        const uint32_t phase_index = (bass_.age / period) & 3u;
        const int8_t arp_up[4] = {0, 7, 12, 3};
        const int8_t arp_down[4] = {12, 7, 3, 0};
        const bool reverse = (motion_direction_.load() == 1 || motion_direction_.load() == 3);
        const int8_t semis = reverse ? arp_down[phase_index] : arp_up[phase_index];
        render_freq *= std::pow(2.0f, static_cast<float>(semis) / 12.0f);
    }
    bass_.phase += render_freq / static_cast<float>(sample_rate_);
    bass_.phase -= std::floor(bass_.phase);

    const float saw = (bass_.phase * 2.0f) - 1.0f;
    const float square = bass_.phase < 0.5f ? 1.0f : -1.0f;
    const float digi = std::floor(saw * 5.0f) / 5.0f;
    const float acid = (saw * 0.48f) + (square * 0.52f);
    float osc = saw;
    if (wave < 0.5f) {
        osc = saw;
    } else if (wave < 1.5f) {
        osc = square;
    } else if (wave < 2.5f) {
        osc = acid;
    } else {
        osc = digi;
    }

    const float sub_phase = std::fmod(bass_.phase * 0.5f, 1.0f);
    const float sub_osc = sub_phase < 0.5f ? 1.0f : -1.0f;
    float sample = (osc * (1.0f - sub * 0.55f)) + (sub_osc * sub * 0.55f);
    const uint32_t attack_samples = std::max<uint32_t>(1, static_cast<uint32_t>(
                                                           static_cast<float>(sample_rate_) * (0.0015f + attack * 0.085f)));
    const float attack_gain = std::min(1.0f, static_cast<float>(bass_.age) / static_cast<float>(attack_samples));
    sample *= attack_gain;

    const float accent_boost = bass_.accent ? 1.55f : 1.0f;
    const float env_push = bass_.amp_env * (0.02f + env * 0.18f);
    const float cutoff_coeff = std::clamp(0.012f + cutoff * cutoff * 0.42f + env_push + (bass_.accent ? 0.08f : 0.0f), 0.01f, 0.62f);
    bass_.filter += (sample - bass_.filter) * cutoff_coeff;
    sample = bass_.filter + ((sample - bass_.filter) * resonance * 0.72f);

    sample = std::tanh(sample * (1.0f + drive * 8.5f) * accent_boost);
    if (fx_select == 1) {
        sample = std::tanh(sample * (1.0f + fx_amount * kBassFxDriveGain));
    } else if (fx_select == 2) {
        const float delayed = bass_.fx_delay[bass_.fx_write];
        bass_.fx_delay[bass_.fx_write] = std::clamp(sample + delayed * (0.22f + fx_amount * 0.55f), -0.95f, 0.95f);
        bass_.fx_write = (bass_.fx_write + 1) % bass_.fx_delay.size();
        sample = std::clamp(sample + delayed * fx_amount * 0.72f, -1.0f, 1.0f);
    } else if (fx_select == 3) {
        const float steps = 3.0f + (1.0f - fx_amount) * 35.0f;
        sample = std::floor(sample * steps) / steps;
    }
    const float sustain_level = 0.08f + sustain * 0.66f;
    const float decay_rate = 0.9966f + decay * 0.0031f;
    const float release_rate = kReleaseFastRate + release * (kReleaseSlowRate - kReleaseFastRate);
    const float envelope_rate = bass_.gate ? decay_rate : release_rate;
    bass_.amp_env *= std::clamp(envelope_rate - env * 0.00016f, 0.9948f, 0.99972f);
    if (bass_.gate && bass_.amp_env < sustain_level) {
        bass_.amp_env = sustain_level;
    }
    sample *= bass_.amp_env;

    const float dc = sample - bass_.dc_x + 0.995f * bass_.dc_y;
    bass_.dc_x = sample;
    bass_.dc_y = dc;
    ++bass_.age;
    return std::clamp(dc * kBassOutputGain, -0.95f, 0.95f);
}

float AudioEngine::apply_mix_eq(float sample, MixEqBus bus, uint8_t mask)
{
    if (mask == 0) {
        return sample;
    }

    const auto index = std::min<std::size_t>(static_cast<std::size_t>(bus), kMixEqBusCount - 1);
    auto& low = mix_eq_low_[index];
    auto& mid = mix_eq_mid_[index];
    auto& high = mix_eq_high_[index];
    low += (sample - low) * 0.038f;
    mid += (sample - mid) * 0.155f;
    high += (sample - high) * 0.42f;

    const float low_band = low;
    const float mid_band = mid - low;
    const float high_band = sample - high;
    float boosted = sample;
    if ((mask & (1u << static_cast<uint8_t>(MixEqBand::Low))) != 0) {
        boosted += low_band * 0.24f;
    }
    if ((mask & (1u << static_cast<uint8_t>(MixEqBand::Mid))) != 0) {
        boosted += mid_band * 0.28f;
    }
    if ((mask & (1u << static_cast<uint8_t>(MixEqBand::High))) != 0) {
        boosted += high_band * 0.20f;
    }
    return std::clamp(boosted, -1.25f, 1.25f);
}

const Pattern* AudioEngine::current_pattern() const
{
    if (catalog_ == nullptr || catalog_->empty()) {
        return nullptr;
    }
    return &catalog_->at(pattern_index_.load());
}

uint32_t AudioEngine::next_step_samples(std::size_t step) const
{
    return samples_for_step(sample_rate_, bpm_.load(), swing_.load(), step);
}

void AudioEngine::write_generated_bass(uint8_t style, uint8_t root, uint32_t seed, uint8_t energy, bool queued,
                                       uint8_t direction, uint16_t period_ms)
{
    write_generated_bass_variant(style, root, seed, energy, queued, direction, period_ms, 62);
}

void AudioEngine::write_generated_bass_variant(uint8_t style, uint8_t root, uint32_t seed, uint8_t energy, bool queued,
                                               uint8_t direction, uint16_t period_ms, uint8_t preserve_hint)
{
    const auto& spec = kBassStyleSpecs[style % kBassStyleCount];
    const Pattern* drum = current_pattern();
    const bool has_motion = direction < 6;
    const bool fast_motion = has_motion && period_ms > 0 && period_ms < 260;
    const bool mid_motion = has_motion && period_ms >= 260 && period_ms < 560;
    const bool slow_motion = has_motion && period_ms >= 560;
    const int period_density = fast_motion ? 18 : (slow_motion ? -14 : 0);
    const int density = std::min<int>(94, spec.density + static_cast<int>(energy) * 12 + period_density);
    const int accent_rate = std::min<int>(82, spec.accent_rate + static_cast<int>(energy) * 8 +
                                          ((direction == 4 || direction == 5) ? 12 : 0));
    const int slide_rate = std::min<int>(76, spec.slide_rate + static_cast<int>(energy) * 10 +
                                         ((direction == 4 || direction == 5) ? 18 : 0));
    auto& note_array = queued ? queued_bass_note_ : bass_note_;
    auto& gate_array = queued ? queued_bass_gate_ : bass_gate_;
    auto& accent_array = queued ? queued_bass_accent_ : bass_accent_;
    auto& slide_array = queued ? queued_bass_slide_ : bass_slide_;
    bool has_previous = false;
    for (std::size_t step = 0; step < kMaxSteps; ++step) {
        has_previous = has_previous || bass_gate_[step].load() != 0;
    }

    for (std::size_t step = 0; step < kMaxSteps; ++step) {
        int chance = density;
        if (drum != nullptr) {
            if (drum->hit(DrumVoice::Kick, step) != 0) {
                chance += spec.rhythm_bias == 7 ? 28 : 12;
            }
            if (drum->hit(DrumVoice::Snare, step) != 0 || drum->hit(DrumVoice::Clap, step) != 0) {
                chance -= 12;
            }
            if (drum->hit(DrumVoice::ClosedHat, step) != 0) {
                chance += spec.rhythm_bias == 8 ? 15 : 4;
            }
        }
        if (spec.rhythm_bias == 9 && (step % 4) == 2) {
            chance += 18;
        }
        if (spec.rhythm_bias == 10 && (step % 8) > 3) {
            chance -= 24;
        }
        if (spec.rhythm_bias == 13 && (step % 4) != 0) {
            chance -= 32;
        }
        if (has_motion) {
            if (direction == 2 && (step % 4) == 1) {
                chance += 18;
            } else if (direction == 3 && (step % 4) == 2) {
                chance += 18;
            } else if ((direction == 4 || direction == 5) && (step % 8) == 7) {
                chance += 24;
            }
            if (fast_motion && (step % 2) == 1) {
                chance += 12;
            }
            if (mid_motion && ((step + static_cast<std::size_t>(direction)) % 6) == 0) {
                chance += 14;
            }
            if (slow_motion && (step % 4) != 0) {
                chance -= 22;
            }
        }

        bool gate = (static_cast<int>(next_rng(seed) % 100) < std::clamp(chance, 10, 96));
        int scale_index = static_cast<int>(step / 4 + static_cast<int>(next_rng(seed) % 3) + style) % 7;
        if (direction == 0) {
            scale_index = (scale_index + static_cast<int>(step / 8)) % 7;
        } else if (direction == 1) {
            scale_index = (scale_index + 7 - static_cast<int>(step / 8)) % 7;
        } else if (direction == 2 || direction == 3) {
            scale_index = (scale_index + (step % 8 == 0 ? 4 : 0)) % 7;
        }
        const int degree = kMinorScale[scale_index];
        const int octave = 36 + static_cast<int>(root % kRootCount) + (spec.octave_bias * 12) +
                           ((next_rng(seed) % 100) < 18 + energy * 8 + ((direction == 5) ? 12 : 0) ? 12 : 0);
        uint8_t note = clamp_u8(octave + degree, 24, 60);
        bool accent = gate && (static_cast<int>(next_rng(seed) % 100) < accent_rate || (step % 8) == 0 ||
                               (fast_motion && (step % 4) == 3));
        bool slide = gate && step > 0 && (static_cast<int>(next_rng(seed) % 100) < slide_rate ||
                                          (has_motion && direction == 4 && (step % 4) == 2));

        if (has_previous) {
            const bool previous_gate = bass_gate_[step].load() != 0;
            const uint8_t previous_note = bass_note_[step].load();
            int keep_chance = static_cast<int>(preserve_hint) - static_cast<int>(energy) * 8;
            if ((step % 8) == 0) {
                keep_chance += 22;
            } else if ((step % 4) == 0) {
                keep_chance += 12;
            }
            if (!previous_gate) {
                keep_chance -= 18;
            }
            if (has_motion && (direction == 0 || direction == 1)) {
                keep_chance -= 10;
            }
            keep_chance = std::clamp(keep_chance, 16, 90);

            if (static_cast<int>(next_rng(seed) % 100) < keep_chance) {
                gate = previous_gate;
                note = previous_note;
                accent = gate && (bass_accent_[step].load() != 0 ||
                                  static_cast<int>(next_rng(seed) % 100) < std::max(8, accent_rate / 3));
                slide = gate && step > 0 && (bass_slide_[step].load() != 0 ||
                                             static_cast<int>(next_rng(seed) % 100) < std::max(4, slide_rate / 4));
                if (gate && energy > 0 && static_cast<int>(next_rng(seed) % 100) < (energy == 2 ? 22 : 10)) {
                    const int move = direction == 1 || direction == 3 ? -2 : 2;
                    note = clamp_u8(static_cast<int>(note) + move, 24, 60);
                }
            }
        }

        if ((step % 16) == 0) {
            gate = true;
            if (!has_previous || energy > 0) {
                note = clamp_u8(36 + static_cast<int>(root % kRootCount) + (spec.octave_bias * 12), 24, 60);
            }
            accent = true;
            slide = false;
        }

        note_array[step].store(note);
        gate_array[step].store(gate ? 1 : 0);
        accent_array[step].store(accent ? 1 : 0);
        slide_array[step].store(slide ? 1 : 0);
    }
}

void AudioEngine::apply_queued_bass_if_needed(std::size_t step)
{
    if ((step % 16) != 0 || !bass_queued_.load()) {
        return;
    }
    for (std::size_t i = 0; i < kMaxSteps; ++i) {
        bass_note_[i].store(queued_bass_note_[i].load());
        bass_gate_[i].store(queued_bass_gate_[i].load());
        bass_accent_[i].store(queued_bass_accent_[i].load());
        bass_slide_[i].store(queued_bass_slide_[i].load());
    }
    bass_queued_.store(false);
}

void AudioEngine::advance_motion_capture()
{
    const auto cooldown = motion_capture_cooldown_steps_.load();
    if (cooldown > 0) {
        motion_capture_cooldown_steps_.store(static_cast<uint16_t>(cooldown - 1));
    }

    if (!motion_capture_active_.load()) {
        return;
    }

    const uint16_t steps = static_cast<uint16_t>(motion_capture_steps_.fetch_add(1) + 1);
    motion_capture_bars_.store(static_cast<uint8_t>(std::min<uint16_t>(kMotionCaptureBars, steps / 16)));
    if (steps >= static_cast<uint16_t>(kMotionCaptureBars) * 16u) {
        finish_motion_capture();
    }
}

}  // namespace tab5drum
