#include "transport.hpp"

#include <algorithm>
#include <cmath>

namespace tab5drum {

uint16_t clamp_bpm(int bpm)
{
    return static_cast<uint16_t>(std::clamp(bpm, static_cast<int>(kMinBpm), static_cast<int>(kMaxBpm)));
}

uint8_t clamp_swing(int swing)
{
    return static_cast<uint8_t>(std::clamp(swing, static_cast<int>(kMinSwing), static_cast<int>(kMaxSwing)));
}

uint32_t samples_for_step(uint32_t sample_rate, uint16_t bpm, uint8_t swing, std::size_t step_index)
{
    const double safe_bpm = clamp_bpm(bpm);
    const double quarter_note = (static_cast<double>(sample_rate) * 60.0) / safe_bpm;
    const double straight_step = quarter_note / 4.0;

    const auto safe_swing = clamp_swing(swing);
    if (safe_swing == 50) {
        return std::max<uint32_t>(1, static_cast<uint32_t>(std::lround(straight_step)));
    }

    const double pair = straight_step * 2.0;
    const double first = pair * (static_cast<double>(safe_swing) / 100.0);
    const double second = pair - first;
    const double selected = (step_index % 2 == 0) ? first : second;
    return std::max<uint32_t>(1, static_cast<uint32_t>(std::lround(selected)));
}

void TapTempo::reset()
{
    for (auto& tap : taps_) {
        tap = 0;
    }
    count_ = 0;
    next_ = 0;
}

bool TapTempo::tap(uint64_t now_ms, uint16_t* out_bpm)
{
    if (count_ > 0) {
        const auto last_index = (next_ + kTapCount - 1) % kTapCount;
        const auto interval = now_ms - taps_[last_index];
        if (interval < 120 || interval > 2000) {
            reset();
        }
    }

    taps_[next_] = now_ms;
    next_ = (next_ + 1) % kTapCount;
    if (count_ < kTapCount) {
        ++count_;
    }

    if (count_ < 2 || out_bpm == nullptr) {
        return false;
    }

    const auto oldest = (next_ + kTapCount - count_) % kTapCount;
    uint64_t total_interval = 0;
    for (std::size_t i = 1; i < count_; ++i) {
        const auto prev = (oldest + i - 1) % kTapCount;
        const auto current = (oldest + i) % kTapCount;
        total_interval += taps_[current] - taps_[prev];
    }

    const auto average_interval = total_interval / (count_ - 1);
    if (average_interval == 0) {
        return false;
    }

    *out_bpm = clamp_bpm(static_cast<int>(60000 / average_interval));
    return true;
}

}  // namespace tab5drum
