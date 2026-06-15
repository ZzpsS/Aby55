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
    esp_err_t set_volume(uint8_t volume);
    esp_err_t read_headphone_inserted(bool* inserted);
    esp_err_t update_headphone_route();
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
};

}  // namespace tab5drum
