#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_err.h"
#include "lvgl.h"

namespace tab5drum {

class Tab5BspAdapter {
public:
    esp_err_t init_display();
    esp_err_t init_audio(uint32_t sample_rate, uint8_t volume);
    esp_err_t disable_unused_hardware();
    esp_err_t set_volume(uint8_t volume);
    esp_err_t apply_volume_curve();
    esp_err_t dump_codec_regs();
    esp_err_t read_codec_reg(uint8_t reg, int* value);
    esp_err_t set_codec_mute_safe(bool muted);
    esp_err_t read_headphone_inserted(bool* inserted);
    esp_err_t update_headphone_route();
    bool headphone_inserted() const;
    esp_err_t write_audio(const int16_t* stereo_samples, std::size_t frames);
    bool lock_display(uint32_t timeout_ms);
    void unlock_display();
    lv_obj_t* screen() const;

private:
    lv_display_t* display_ = nullptr;
    void* speaker_codec_ = nullptr;
    uint32_t sample_rate_ = 44100;
    bool speaker_enabled_ = true;
    bool speaker_route_known_ = false;
    bool headphone_inserted_ = false;
};

}  // namespace tab5drum
