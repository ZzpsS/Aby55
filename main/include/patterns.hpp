#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace tab5drum {

constexpr std::size_t kMaxSteps = 32;

enum class DrumVoice : uint8_t {
    Kick = 0,
    Snare,
    Clap,
    ClosedHat,
    OpenHat,
    Perc,
    Count,
};

constexpr std::size_t kVoiceCount = static_cast<std::size_t>(DrumVoice::Count);

const char* voice_name(DrumVoice voice);
bool voice_from_name(const char* name, DrumVoice* out_voice);

struct Pattern {
    std::string id;
    std::string name;
    uint8_t steps = 16;
    uint16_t default_bpm = 120;
    uint8_t default_swing = 50;
    std::array<std::array<uint8_t, kMaxSteps>, kVoiceCount> lanes = {};

    uint8_t hit(DrumVoice voice, std::size_t step) const;
    bool any_hit(std::size_t step) const;
};

class PatternCatalog {
public:
    bool load_builtin();
    bool empty() const { return patterns_.empty(); }
    std::size_t size() const { return patterns_.size(); }
    const Pattern& at(std::size_t index) const;
    Pattern& mutable_at(std::size_t index);
    const std::vector<Pattern>& patterns() const { return patterns_; }

private:
    std::vector<Pattern> patterns_;
};

}  // namespace tab5drum
