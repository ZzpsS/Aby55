#include "patterns.hpp"

#include <cstring>

#include "cJSON.h"
#include "esp_log.h"

extern const uint8_t patterns_json_start[] asm("_binary_patterns_json_start");
extern const uint8_t patterns_json_end[] asm("_binary_patterns_json_end");

namespace tab5drum {
namespace {

const char* const kVoiceNames[] = {
    "kick",
    "snare",
    "clap",
    "closed_hat",
    "open_hat",
    "perc",
};

const char* const kTag = "patterns";

uint16_t json_u16(const cJSON* object, const char* key, uint16_t fallback)
{
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsNumber(item) ? static_cast<uint16_t>(item->valueint) : fallback;
}

const char* json_string(const cJSON* object, const char* key)
{
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsString(item) ? item->valuestring : "";
}

}  // namespace

const char* voice_name(DrumVoice voice)
{
    const auto index = static_cast<std::size_t>(voice);
    return index < kVoiceCount ? kVoiceNames[index] : "unknown";
}

bool voice_from_name(const char* name, DrumVoice* out_voice)
{
    if (name == nullptr || out_voice == nullptr) {
        return false;
    }

    for (std::size_t i = 0; i < kVoiceCount; ++i) {
        if (std::strcmp(name, kVoiceNames[i]) == 0) {
            *out_voice = static_cast<DrumVoice>(i);
            return true;
        }
    }

    return false;
}

uint8_t Pattern::hit(DrumVoice voice, std::size_t step) const
{
    if (step >= steps || step >= kMaxSteps) {
        return 0;
    }
    return lanes[static_cast<std::size_t>(voice)][step];
}

bool Pattern::any_hit(std::size_t step) const
{
    for (std::size_t voice = 0; voice < kVoiceCount; ++voice) {
        if (step < kMaxSteps && lanes[voice][step] != 0) {
            return true;
        }
    }
    return false;
}

const Pattern& PatternCatalog::at(std::size_t index) const
{
    static const Pattern kFallback = [] {
        Pattern pattern;
        pattern.id = "fallback";
        pattern.name = "Fallback";
        pattern.steps = 16;
        pattern.default_bpm = 120;
        pattern.default_swing = 50;
        return pattern;
    }();

    if (patterns_.empty()) {
        return kFallback;
    }
    return patterns_[index % patterns_.size()];
}

Pattern& PatternCatalog::mutable_at(std::size_t index)
{
    static Pattern kFallback = [] {
        Pattern pattern;
        pattern.id = "fallback";
        pattern.name = "Fallback";
        pattern.steps = 16;
        pattern.default_bpm = 120;
        pattern.default_swing = 50;
        return pattern;
    }();

    if (patterns_.empty()) {
        return kFallback;
    }
    return patterns_[index % patterns_.size()];
}

bool PatternCatalog::load_builtin()
{
    patterns_.clear();

    const auto* json_begin = reinterpret_cast<const char*>(patterns_json_start);
    const auto json_len = static_cast<std::size_t>(patterns_json_end - patterns_json_start);
    cJSON* root = cJSON_ParseWithLength(json_begin, json_len);
    if (!cJSON_IsArray(root)) {
        ESP_LOGE(kTag, "patterns.json is not an array");
        cJSON_Delete(root);
        return false;
    }

    cJSON* item = nullptr;
    cJSON_ArrayForEach(item, root)
    {
        if (!cJSON_IsObject(item)) {
            continue;
        }

        Pattern pattern;
        pattern.id = json_string(item, "id");
        pattern.name = json_string(item, "name");
        const uint8_t json_steps = static_cast<uint8_t>(json_u16(item, "steps", 16));
        pattern.steps = json_steps;
        pattern.default_bpm = json_u16(item, "default_bpm", 120);
        pattern.default_swing = static_cast<uint8_t>(json_u16(item, "default_swing", 50));

        if ((json_steps != 16 && json_steps != 32) || pattern.id.empty() || pattern.name.empty()) {
            ESP_LOGW(kTag, "skipping invalid pattern entry");
            continue;
        }

        const cJSON* lanes = cJSON_GetObjectItemCaseSensitive(item, "lanes");
        if (cJSON_IsObject(lanes)) {
            cJSON* lane = nullptr;
            cJSON_ArrayForEach(lane, lanes)
            {
                DrumVoice voice = DrumVoice::Kick;
                if (!voice_from_name(lane->string, &voice) || !cJSON_IsArray(lane)) {
                    continue;
                }

                auto& steps = pattern.lanes[static_cast<std::size_t>(voice)];
                const auto limit = static_cast<std::size_t>(json_steps);
                for (std::size_t step = 0; step < limit; ++step) {
                    const cJSON* value = cJSON_GetArrayItem(lane, static_cast<int>(step));
                    if (cJSON_IsNumber(value)) {
                        const int raw = value->valueint;
                        steps[step] = raw < 0 ? 0 : (raw > 2 ? 2 : static_cast<uint8_t>(raw));
                    }
                }
            }
        }

        if (json_steps == 16) {
            for (std::size_t voice = 0; voice < kVoiceCount; ++voice) {
                for (std::size_t step = 0; step < 16; ++step) {
                    pattern.lanes[voice][step + 16] = pattern.lanes[voice][step];
                }
            }
            pattern.steps = 32;
        }

        patterns_.push_back(pattern);
    }

    cJSON_Delete(root);
    ESP_LOGI(kTag, "loaded %u built-in patterns", static_cast<unsigned>(patterns_.size()));
    return !patterns_.empty();
}

}  // namespace tab5drum
