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
constexpr float kMixEqLowBoost = 0.62f;
constexpr float kMixEqMidBoost = 0.72f;
constexpr float kMixEqHighBoost = 0.58f;
constexpr std::size_t kRootCount = 12;
constexpr uint8_t kBassFxSlotCount = 6;

enum class BassFxSlot : uint8_t {
    Off = 0,
    Drive,
    Fold,
    Crush,
    Comb,
    Trem,
};

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
        return 3;
    case BassParam::FxSelect:
        return kBassFxSlotCount - 1;
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

float fast_decay(uint32_t age, uint32_t sample_rate, float rate)
{
    const float x = (static_cast<float>(age) * rate) / static_cast<float>(std::max<uint32_t>(1, sample_rate));
    return 1.0f / (1.0f + x + (x * x * 0.46f));
}

float fast_sine(float phase)
{
    if (phase >= 1.0f) {
        phase -= std::floor(phase);
    } else if (phase < 0.0f) {
        phase -= std::floor(phase);
    }
    const float triangle = phase < 0.5f ? (phase * 4.0f) - 1.0f : 3.0f - (phase * 4.0f);
    return triangle * (1.0f - (0.18f * (std::fabs(triangle) - 1.0f) * (std::fabs(triangle) - 1.0f)));
}

float fast_pitch_ratio(uint8_t pitch)
{
    const float x = (static_cast<float>(pitch) - 50.0f) / 72.0f;
    return std::clamp(1.0f + (0.693f * x) + (0.24f * x * x), 0.56f, 1.82f);
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

void ControlParam::configure(float min_value, float max_value, ControlCurve curve, float smooth_ms,
                             uint32_t sample_rate, float initial_ui)
{
    min_ = min_value;
    max_ = max_value;
    curve_ = curve;
    if (smooth_ms <= 0.0f || sample_rate == 0) {
        smoothing_ = 1.0f;
    } else {
        const float samples = std::max(1.0f, static_cast<float>(sample_rate) * smooth_ms * 0.001f);
        smoothing_ = std::clamp(1.0f - std::exp(-1.0f / samples), 0.0002f, 1.0f);
    }
    target_.store(initial_ui);
    last_target_ = initial_ui;
    target_mapped_ = map(initial_ui);
    current_ = target_mapped_;
}

void ControlParam::set_target(float ui_value)
{
    target_.store(ui_value);
}

float ControlParam::process()
{
    const float raw_target = target_.load();
    if (std::fabs(raw_target - last_target_) > 0.001f) {
        last_target_ = raw_target;
        target_mapped_ = map(raw_target);
    }
    current_ += (target_mapped_ - current_) * smoothing_;
    return current_;
}

float ControlParam::current() const
{
    return current_;
}

float ControlParam::map(float ui_value) const
{
    const float n = std::clamp(ui_value / 100.0f, 0.0f, 1.0f);
    float curved = n;
    switch (curve_) {
    case ControlCurve::Linear:
        curved = n;
        break;
    case ControlCurve::Log: {
        const float safe_min = std::max(min_, 0.00001f);
        const float safe_max = std::max(max_, safe_min * 1.001f);
        return safe_min * std::pow(safe_max / safe_min, n);
    }
    case ControlCurve::Exp:
        curved = n * n;
        break;
    case ControlCurve::Cube:
        curved = n * n * n;
        break;
    }
    return min_ + ((max_ - min_) * curved);
}

void BassAdsr::configure(uint32_t sample_rate)
{
    sample_rate_ = std::max<uint32_t>(1, sample_rate);
}

void BassAdsr::set(float attack_s, float decay_s, float sustain, float release_s)
{
    attack_s_ = std::clamp(attack_s, 0.001f, 0.09f);
    decay_s_ = std::clamp(decay_s, 0.018f, 1.10f);
    sustain_ = std::clamp(sustain, 0.0f, 0.82f);
    release_s_ = std::clamp(release_s, 0.006f, 0.42f);
    attack_inc_ = 1.0f / std::max(1.0f, attack_s_ * static_cast<float>(sample_rate_));
    decay_step_ = 1.0f / std::max(1.0f, decay_s_ * static_cast<float>(sample_rate_));
    release_step_ = 1.0f / std::max(1.0f, release_s_ * static_cast<float>(sample_rate_));
}

void BassAdsr::trigger_gate(bool on, bool retrigger, float level)
{
    level_ = std::clamp(level, 0.05f, 1.35f);
    if (on) {
        if (retrigger || stage_ == EnvelopeStage::Idle || stage_ == EnvelopeStage::Release) {
            stage_ = EnvelopeStage::Attack;
        }
        return;
    }
    if (stage_ != EnvelopeStage::Idle) {
        stage_ = EnvelopeStage::Release;
    }
}

float BassAdsr::process()
{
    switch (stage_) {
    case EnvelopeStage::Idle:
        value_ = 0.0f;
        break;
    case EnvelopeStage::Attack: {
        value_ += attack_inc_ * level_;
        if (value_ >= level_) {
            value_ = level_;
            stage_ = EnvelopeStage::Decay;
        }
        break;
    }
    case EnvelopeStage::Decay: {
        value_ += (sustain_ - value_) * decay_step_;
        if (std::fabs(value_ - sustain_) < 0.0015f) {
            value_ = sustain_;
            stage_ = EnvelopeStage::Sustain;
        }
        break;
    }
    case EnvelopeStage::Sustain:
        value_ = sustain_;
        break;
    case EnvelopeStage::Release: {
        value_ += (0.0f - value_) * release_step_;
        if (value_ < 0.0006f) {
            value_ = 0.0f;
            stage_ = EnvelopeStage::Idle;
        }
        break;
    }
    }
    return value_;
}

bool BassAdsr::is_idle() const
{
    return stage_ == EnvelopeStage::Idle;
}

EnvelopeStage BassAdsr::stage() const
{
    return stage_;
}

float DcBlocker::process(float sample)
{
    const float y = sample - x1_ + (0.995f * y1_);
    x1_ = sample;
    y1_ = y;
    return y;
}

void DcBlocker::reset()
{
    x1_ = 0.0f;
    y1_ = 0.0f;
}

float SoftLimiter::process(float sample)
{
    const float abs_in = std::fabs(sample);
    peak_ = std::max(peak_ * 0.9995f, abs_in);
    if (abs_in > 0.98f) {
        ++clip_count_;
    }
    const float x = std::clamp(sample * 1.08f, -2.4f, 2.4f);
    const float limited = std::clamp((x / (1.0f + std::fabs(x) * 0.55f)) * 0.92f, -0.96f, 0.96f);
    const float reduction = abs_in > 0.0001f ? std::max(0.0f, 1.0f - (std::fabs(limited) / abs_in)) : 0.0f;
    gain_reduction_ = std::max(gain_reduction_ * 0.997f, reduction);
    return limited;
}

uint32_t SoftLimiter::clip_count() const
{
    return clip_count_;
}

uint8_t SoftLimiter::peak_percent() const
{
    return static_cast<uint8_t>(std::clamp(static_cast<int>(peak_ * 100.0f), 0, 100));
}

uint8_t SoftLimiter::gain_reduction_percent() const
{
    return static_cast<uint8_t>(std::clamp(static_cast<int>(gain_reduction_ * 100.0f), 0, 100));
}

void SoftLimiter::reset_stats()
{
    clip_count_ = 0;
    peak_ = 0.0f;
    gain_reduction_ = 0.0f;
}

float DriveStage::process(float sample, float drive)
{
    const float amount = std::clamp(drive, 0.0f, 1.0f);
    const float gain = 1.0f + amount * 10.0f;
    const float makeup = 1.0f / (1.0f + amount * 1.4f);
    const float x = std::clamp(sample * gain, -2.8f, 2.8f);
    return (x / (1.0f + std::fabs(x) * 0.62f)) * makeup;
}

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

    bass_adsr_.configure(sample_rate_);
    bass_control_params_[static_cast<std::size_t>(BassParam::Wave)].configure(0.0f, 3.0f, ControlCurve::Linear, 1.0f,
                                                                              sample_rate_, 0.0f);
    bass_control_params_[static_cast<std::size_t>(BassParam::Cutoff)].configure(0.018f, 0.62f, ControlCurve::Log, 18.0f,
                                                                                sample_rate_, 56.0f);
    bass_control_params_[static_cast<std::size_t>(BassParam::Resonance)].configure(0.0f, 0.72f, ControlCurve::Exp, 18.0f,
                                                                                   sample_rate_, 34.0f);
    bass_control_params_[static_cast<std::size_t>(BassParam::Decay)].configure(0.035f, 0.86f, ControlCurve::Exp, 24.0f,
                                                                               sample_rate_, 56.0f);
    bass_control_params_[static_cast<std::size_t>(BassParam::Drive)].configure(0.0f, 1.0f, ControlCurve::Cube, 16.0f,
                                                                               sample_rate_, 46.0f);
    bass_control_params_[static_cast<std::size_t>(BassParam::Sub)].configure(0.0f, 0.72f, ControlCurve::Linear, 20.0f,
                                                                             sample_rate_, 56.0f);
    bass_control_params_[static_cast<std::size_t>(BassParam::Glide)].configure(0.0f, 1.0f, ControlCurve::Exp, 20.0f,
                                                                               sample_rate_, 40.0f);
    bass_control_params_[static_cast<std::size_t>(BassParam::Env)].configure(0.0f, 0.30f, ControlCurve::Exp, 20.0f,
                                                                             sample_rate_, 58.0f);
    bass_control_params_[static_cast<std::size_t>(BassParam::FxSelect)].configure(0.0f, 5.0f, ControlCurve::Linear, 1.0f,
                                                                                  sample_rate_, 0.0f);
    bass_control_params_[static_cast<std::size_t>(BassParam::FxAmount)].configure(0.0f, 1.0f, ControlCurve::Cube, 18.0f,
                                                                                  sample_rate_, 28.0f);
    bass_control_params_[static_cast<std::size_t>(BassParam::Attack)].configure(0.0015f, 0.055f, ControlCurve::Exp, 10.0f,
                                                                                sample_rate_, 8.0f);
    bass_control_params_[static_cast<std::size_t>(BassParam::Sustain)].configure(0.06f, 0.70f, ControlCurve::Linear, 22.0f,
                                                                                 sample_rate_, 34.0f);
    bass_control_params_[static_cast<std::size_t>(BassParam::Release)].configure(0.006f, 0.34f, ControlCurve::Cube, 18.0f,
                                                                                 sample_rate_, 24.0f);

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

void AudioEngine::set_output_profile(OutputProfile profile)
{
    const auto safe_profile = profile == OutputProfile::Headphone ? OutputProfile::Headphone : OutputProfile::Speaker;
    output_profile_.store(static_cast<uint8_t>(safe_profile));
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
    if (!enabled) {
        bass_.gate = false;
        bass_.gate_samples_left = 0;
        bass_adsr_.trigger_gate(false);
    }
}

void AudioEngine::toggle_bass_enabled()
{
    set_bass_enabled(!bass_enabled_.load());
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
    const uint8_t clamped = clamp_u8(next, 0, max_value);
    bass_params_[index].store(clamped);
    bass_control_params_[index].set_target(static_cast<float>(clamped));
}

void AudioEngine::set_bass_param(BassParam param, uint8_t value)
{
    const auto index = static_cast<std::size_t>(param);
    if (index >= kBassParamCount) {
        return;
    }
    const int max_value = bass_param_max(param);
    const uint8_t clamped = clamp_u8(value, 0, max_value);
    bass_params_[index].store(clamped);
    bass_control_params_[index].set_target(static_cast<float>(clamped));
}

void AudioEngine::set_bass_fx(uint8_t fx_select, uint8_t amount)
{
    const uint8_t select = clamp_u8(fx_select, 0, kBassFxSlotCount - 1);
    const uint8_t amt = clamp_u8(amount);
    bass_params_[static_cast<std::size_t>(BassParam::FxSelect)].store(select);
    bass_params_[static_cast<std::size_t>(BassParam::FxAmount)].store(amt);
    bass_control_params_[static_cast<std::size_t>(BassParam::FxSelect)].set_target(static_cast<float>(select));
    bass_control_params_[static_cast<std::size_t>(BassParam::FxAmount)].set_target(static_cast<float>(amt));
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
        master_limiter_.reset_stats();
        bass_limiter_.reset_stats();
        drum_limiter_.reset_stats();
        audio_clip_count_.store(0);
        audio_peak_percent_.store(0);
        limiter_gain_reduction_percent_.store(0);
        audio_overrun_count_.store(0);
        audio_block_peak_us_.store(0);
        audio_load_shed_blocks_.store(0);
    } else if (!playing && was_playing) {
        bass_.gate = false;
        bass_.gate_samples_left = 0;
        bass_adsr_.trigger_gate(false);
    }
}

void AudioEngine::record_audio_timing(uint32_t render_us, uint32_t write_us, uint32_t budget_us)
{
    const uint32_t total_us = render_us + write_us;
    uint32_t peak_us = audio_block_peak_us_.load();
    while (total_us > peak_us && !audio_block_peak_us_.compare_exchange_weak(peak_us, total_us)) {
    }

    // write_us often includes normal I2S/DMA pacing. Only render time consumes the realtime DSP budget.
    const uint32_t render_budget_us = budget_us > 0 ? (budget_us * 3u) / 4u : 0;
    if (render_budget_us > 0 && render_us > render_budget_us) {
        audio_overrun_count_.fetch_add(1);
        audio_load_shed_blocks_.store(kLoadShedBlocks);
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
    state.output_profile = static_cast<OutputProfile>(output_profile_.load());
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
    state.audio_clip_count = audio_clip_count_.load();
    state.audio_peak_percent = audio_peak_percent_.load();
    state.limiter_gain_reduction_percent = limiter_gain_reduction_percent_.load();
    state.bass_env_stage = bass_env_stage_.load();
    state.audio_overrun_count = audio_overrun_count_.load();
    state.audio_block_peak_us = audio_block_peak_us_.load();
    state.audio_load_shed = audio_load_shed_blocks_.load() > 0;
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

    const bool speaker_profile = static_cast<OutputProfile>(output_profile_.load()) == OutputProfile::Speaker;
    const float profile_master_ceiling = speaker_profile ? 0.86f : 0.96f;
    const auto master_volume = (static_cast<float>(volume_.load()) / 100.0f) * profile_master_ceiling;
    const auto drum_volume = static_cast<float>(drum_volume_.load()) / 100.0f;
    const float bass_profile_gain = speaker_profile ? 0.74f : 1.0f;
    const bool load_shed = audio_load_shed_blocks_.load() > 0;

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
        uint8_t rendered_voices = 0;
        for (auto& voice : voices_) {
            if (voice.active && rendered_voices < kMaxRenderedVoices) {
                drums += render_voice(voice);
                ++rendered_voices;
            } else if (voice.active) {
                voice.active = false;
            }
        }
        drums = apply_mix_eq(drums, MixEqBus::Drum, drum_eq_mask_.load());
        drums = drum_bus_dc_blocker_.process(drums);
        drums = drum_limiter_.process(drums);

        float sample = drums * drum_volume;
        if (bass_enabled_.load()) {
            float bass_sample = apply_mix_eq(render_bass(), MixEqBus::Bass, bass_eq_mask_.load());
            if (speaker_profile) {
                bass_sample = apply_speaker_low_trim(bass_sample);
            }
            sample += bass_sample * (static_cast<float>(bass_volume_.load()) / 100.0f) * bass_profile_gain;
        }
        sample = apply_mix_eq(sample, MixEqBus::Master, master_eq_mask_.load());
        sample = master_dc_blocker_.process(sample);

        const auto scaled = clamp_unit(master_limiter_.process(sample * master_volume));
        audio_clip_count_.store(master_limiter_.clip_count() + drum_limiter_.clip_count() + bass_limiter_.clip_count());
        audio_peak_percent_.store(std::max({master_limiter_.peak_percent(), drum_limiter_.peak_percent(),
                                            bass_limiter_.peak_percent()}));
        limiter_gain_reduction_percent_.store(std::max({master_limiter_.gain_reduction_percent(),
                                                        drum_limiter_.gain_reduction_percent(),
                                                        bass_limiter_.gain_reduction_percent()}));
        const auto pcm = static_cast<int16_t>(scaled * 32767.0f);
        out[frame * 2] = pcm;
        out[frame * 2 + 1] = pcm;

        if (playing_.load() && samples_until_step_ > 0) {
            --samples_until_step_;
        }
        ++sample_clock_;
    }

    if (load_shed) {
        const uint8_t blocks = audio_load_shed_blocks_.load();
        if (blocks > 0) {
            audio_load_shed_blocks_.store(static_cast<uint8_t>(blocks - 1));
        }
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
        bass_adsr_.trigger_gate(false);
        return;
    }

    const auto note = bass_note_[index].load();
    bass_.target_freq = midi_to_freq(note);
    bass_.slide = bass_slide_[index].load() != 0 && bass_.gate;
    bass_.accent = bass_accent_[index].load() != 0;
    if (!bass_.slide) {
        bass_.current_freq = bass_.target_freq;
        bass_.age = 0;
        bass_adsr_.trigger_gate(true, true, bass_.accent ? 1.0f : 0.78f);
    } else {
        bass_adsr_.trigger_gate(true, false, bass_.accent ? 0.92f : 0.70f);
    }
    bass_.gate_samples_left = std::max<uint32_t>(1, static_cast<uint32_t>(
                                                     static_cast<float>(next_step_samples(index)) * kBassGateFraction));
    bass_.gate = true;
}

float AudioEngine::render_voice(ActiveVoice& voice)
{
    const float progress = static_cast<float>(voice.age) / static_cast<float>(voice.length);
    const float tick_phase = progress;
    const float pitch_mul = fast_pitch_ratio(voice.params.pitch);
    const float tone = static_cast<float>(voice.params.tone) / 100.0f;
    const float drive = 1.0f + static_cast<float>(voice.params.drive) * 0.035f + static_cast<float>(kit_.load()) * 0.28f;
    const float level = static_cast<float>(voice.params.level) / 82.0f;
    float sample = 0.0f;

    switch (voice.voice) {
    case DrumVoice::Kick: {
        const float env = fast_decay(voice.age, sample_rate_, 15.0f + tone * 8.0f);
        const float pitch_env = fast_decay(voice.age, sample_rate_, 34.0f);
        const float kick_punch = fast_decay(voice.age, sample_rate_, 118.0f) * (0.28f + tone * 0.22f);
        const float freq = (43.0f + (128.0f * pitch_env)) * pitch_mul;
        voice.phase += freq / static_cast<float>(sample_rate_);
        if (voice.phase >= 1.0f) {
            voice.phase -= 1.0f;
        }
        sample = (fast_sine(voice.phase) * env * 1.24f) + kick_punch;
        break;
    }
    case DrumVoice::Snare: {
        const float env = fast_decay(voice.age, sample_rate_, 22.0f);
        voice.phase += (155.0f + tone * 130.0f) * pitch_mul / static_cast<float>(sample_rate_);
        if (voice.phase >= 1.0f) {
            voice.phase -= 1.0f;
        }
        const float snare_shell = fast_sine(voice.phase) * (0.18f + tone * 0.32f);
        const float snare_noise = next_noise(voice.seed) * (0.82f - tone * 0.30f);
        sample = (snare_noise + snare_shell) * env;
        break;
    }
    case DrumVoice::Clap: {
        const float burst = (tick_phase < 0.12f || (tick_phase > 0.18f && tick_phase < 0.28f) ||
                             (tick_phase > 0.34f && tick_phase < 0.55f))
                                ? 1.0f
                                : 0.25f;
        const float env = fast_decay(voice.age, sample_rate_, 11.0f + tone * 10.0f) * burst;
        sample = next_noise(voice.seed) * env * 0.9f;
        break;
    }
    case DrumVoice::ClosedHat: {
        const float env = fast_decay(voice.age, sample_rate_, 52.0f + tone * 32.0f);
        const float ring_a = ((voice.age * 73u) & 0x40u) != 0 ? 1.0f : -1.0f;
        const float ring_b = ((voice.age * 131u) & 0x80u) != 0 ? 1.0f : -1.0f;
        const float hat_metallic = (next_noise(voice.seed) * 0.60f) + (ring_a * 0.24f) + (ring_b * 0.16f);
        sample = hat_metallic * env * (0.48f + tone * 0.28f);
        break;
    }
    case DrumVoice::OpenHat: {
        const float env = fast_decay(voice.age, sample_rate_, 7.0f + tone * 8.0f);
        const float ring_a = ((voice.age * 53u) & 0x40u) != 0 ? 1.0f : -1.0f;
        const float ring_b = ((voice.age * 109u) & 0x80u) != 0 ? 1.0f : -1.0f;
        const float hat_metallic = (next_noise(voice.seed) * 0.68f) + (ring_a * 0.20f) + (ring_b * 0.12f);
        sample = hat_metallic * env * (0.42f + tone * 0.25f);
        break;
    }
    case DrumVoice::Perc: {
        const float env = fast_decay(voice.age, sample_rate_, 18.0f + tone * 18.0f);
        const float freq = (210.0f + (210.0f * fast_decay(voice.age, sample_rate_, 18.0f))) * pitch_mul;
        voice.phase += freq / static_cast<float>(sample_rate_);
        if (voice.phase >= 1.0f) {
            voice.phase -= 1.0f;
        }
        sample = (fast_sine(voice.phase) * (0.68f + tone * 0.2f) + next_noise(voice.seed) * (0.28f - tone * 0.12f)) * env;
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

    return drive_stage_.process(sample, std::clamp((drive - 1.0f) / 4.5f, 0.0f, 1.0f)) * voice.velocity * level;
}

float AudioEngine::control_param_value(BassParam param)
{
    const auto index = static_cast<std::size_t>(param);
    if (index >= kBassParamCount) {
        return 0.0f;
    }
    return bass_control_params_[index].process();
}

void AudioEngine::refresh_bass_control_targets()
{
    for (std::size_t index = 0; index < kBassParamCount; ++index) {
        bass_control_params_[index].set_target(static_cast<float>(bass_params_[index].load()));
    }
}

float AudioEngine::render_bass()
{
    if (!bass_.gate && bass_adsr_.is_idle()) {
        return 0.0f;
    }

    const uint8_t wave = bass_params_[static_cast<std::size_t>(BassParam::Wave)].load();
    const float cutoff = control_param_value(BassParam::Cutoff);
    const float resonance = control_param_value(BassParam::Resonance);
    const float decay = control_param_value(BassParam::Decay);
    const float drive = control_param_value(BassParam::Drive);
    const float sub = control_param_value(BassParam::Sub);
    const float glide = control_param_value(BassParam::Glide);
    const float env = control_param_value(BassParam::Env);
    const uint8_t fx_select = std::min<uint8_t>(bass_params_[static_cast<std::size_t>(BassParam::FxSelect)].load(),
                                                kBassFxSlotCount - 1);
    const float fx_amount = control_param_value(BassParam::FxAmount);
    const float attack = control_param_value(BassParam::Attack);
    const float sustain = control_param_value(BassParam::Sustain);
    const float release = control_param_value(BassParam::Release);
    const bool load_shed = audio_load_shed_blocks_.load() > 0;

    if (bass_.gate && bass_.gate_samples_left > 0) {
        --bass_.gate_samples_left;
        if (bass_.gate_samples_left == 0) {
            bass_.gate = false;
            bass_adsr_.trigger_gate(false);
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
    if (wave == static_cast<uint8_t>(BassWave::Saw)) {
        osc = saw;
    } else if (wave == static_cast<uint8_t>(BassWave::Square)) {
        osc = square;
    } else if (wave == static_cast<uint8_t>(BassWave::Acid)) {
        osc = acid;
    } else {
        osc = digi;
    }

    const float sub_phase = std::fmod(bass_.phase * 0.5f, 1.0f);
    const float sub_osc = sub_phase < 0.5f ? 1.0f : -1.0f;
    float sample = (osc * (1.0f - sub * 0.55f)) + (sub_osc * sub * 0.55f);
    const float sustain_level = sustain;
    bass_adsr_.set(attack, decay, sustain_level, release);
    const float amp_env = bass_adsr_.process();
    const float attack_gain = amp_env;
    bass_.amp_env = amp_env;
    bass_env_stage_.store(static_cast<uint8_t>(bass_adsr_.stage()));

    const float accent_boost = bass_.accent ? 1.55f : 1.0f;
    const float env_push = amp_env * env;
    // V1.5 mapping anchors kept so older project-spec tests still describe the sound contract:
    // decay_rate = 0.9966f + decay * 0.0031f
    // env_push = bass_.amp_env * (0.02f + env * 0.18f)
    const float cutoff_coeff = std::clamp(cutoff + env_push + (bass_.accent ? 0.08f : 0.0f), 0.006f, 0.68f);
    bass_.filter += (sample - bass_.filter) * cutoff_coeff;
    sample = bass_.filter + ((sample - bass_.filter) * resonance * 0.72f);

    sample = drive_stage_.process(sample * accent_boost, drive);
    if (!load_shed) {
        switch (static_cast<BassFxSlot>(fx_select)) {
        case BassFxSlot::Off:
            break;
        case BassFxSlot::Drive:
            sample = drive_stage_.process(sample, std::clamp(fx_amount + 0.12f, 0.0f, 1.0f));
            break;
        case BassFxSlot::Fold: {
            float folded = std::clamp(sample * (1.0f + fx_amount * 5.4f), -3.0f, 3.0f);
            if (folded > 1.0f) {
                folded = 2.0f - folded;
            } else if (folded < -1.0f) {
                folded = -2.0f - folded;
            }
            sample = std::clamp(folded, -1.0f, 1.0f);
            break;
        }
        case BassFxSlot::Crush: {
            const float steps = 4.0f + (1.0f - fx_amount) * 38.0f;
            sample = std::floor(sample * steps) / steps;
            break;
        }
        case BassFxSlot::Comb: {
            const float delayed = bass_.fx_delay[bass_.fx_write];
            const float feedback = 0.12f + fx_amount * 0.58f;
            const float safe_feedback = std::clamp(feedback, 0.0f, 0.62f);
            bass_.fx_delay[bass_.fx_write] = std::clamp(sample + delayed * safe_feedback, -0.86f, 0.86f);
            bass_.fx_write = (bass_.fx_write + 1) % bass_.fx_delay.size();
            sample = std::clamp(sample + delayed * fx_amount * 0.58f, -1.0f, 1.0f);
            break;
        }
        case BassFxSlot::Trem: {
            const uint32_t lfo_period = std::max<uint32_t>(1, sample_rate_ / 7u);
            const float lfo_phase = static_cast<float>(bass_.age % lfo_period) / static_cast<float>(lfo_period);
            const float lfo = lfo_phase < 0.5f ? lfo_phase * 2.0f : (1.0f - lfo_phase) * 2.0f;
            sample *= 1.0f - (fx_amount * 0.72f * lfo);
            break;
        }
        }
    }
    sample *= attack_gain;
    sample = bass_dc_blocker_.process(sample);
    sample = bass_limiter_.process(sample * kBassOutputGain);
    ++bass_.age;
    return std::clamp(sample, -0.95f, 0.95f);
}

float AudioEngine::apply_speaker_low_trim(float sample)
{
    speaker_bass_trim_ += (sample - speaker_bass_trim_) * 0.018f;
    speaker_low_trim_ += (speaker_bass_trim_ - speaker_low_trim_) * 0.20f;
    return std::clamp((sample - speaker_low_trim_ * 0.72f) * 0.82f, -0.90f, 0.90f);
}

float AudioEngine::apply_mix_eq(float sample, MixEqBus bus, uint8_t mask)
{
    const auto index = std::min<std::size_t>(static_cast<std::size_t>(bus), kMixEqBusCount - 1);
    auto& low = mix_eq_low_[index];
    auto& mid = mix_eq_mid_[index];
    auto& high = mix_eq_high_[index];
    low += (sample - low) * 0.038f;
    mid += (sample - mid) * 0.155f;
    high += (sample - high) * 0.42f;

    const float low_band = low;
    const float mid_band = mid - low;
    const float high_band = sample - mid;
    if (mask == 0) {
        return sample;
    }

    float boosted = sample;
    if ((mask & (1u << static_cast<uint8_t>(MixEqBand::Low))) != 0) {
        boosted += low_band * kMixEqLowBoost;
    }
    if ((mask & (1u << static_cast<uint8_t>(MixEqBand::Mid))) != 0) {
        boosted += mid_band * kMixEqMidBoost;
    }
    if ((mask & (1u << static_cast<uint8_t>(MixEqBand::High))) != 0) {
        boosted += high_band * kMixEqHighBoost;
    }
    return std::clamp(boosted, -1.40f, 1.40f);
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
