#include "tab5_bsp_adapter.hpp"

#include "bsp/m5stack_tab5.h"
#include "esp_codec_dev.h"
#include "esp_log.h"

namespace tab5drum {
namespace {

const char* const kTag = "tab5_bsp";

esp_codec_dev_vol_map_t kAby55VolumeCurve[] = {
    {0, -96.0f},
    {8, -58.0f},
    {24, -36.0f},
    {48, -20.0f},
    {72, -9.0f},
    {90, -4.0f},
    {100, -1.5f},
};

esp_codec_dev_handle_t as_codec(void* handle)
{
    return static_cast<esp_codec_dev_handle_t>(handle);
}

}  // namespace

esp_err_t Tab5BspAdapter::init_display()
{
    ESP_LOGI(kTag, "init_display begin");
    display_ = bsp_display_start();
    if (display_ == nullptr) {
        ESP_LOGE(kTag, "bsp_display_start failed");
        return ESP_FAIL;
    }

    lv_display_set_default(display_);
    bsp_display_backlight_on();
    ESP_LOGI(kTag, "display resolution %ldx%ld",
             static_cast<long>(lv_display_get_horizontal_resolution(display_)),
             static_cast<long>(lv_display_get_vertical_resolution(display_)));
    ESP_LOGI(kTag, "init_display done");
    return ESP_OK;
}

esp_err_t Tab5BspAdapter::init_audio(uint32_t sample_rate, uint8_t volume)
{
    ESP_LOGI(kTag, "init_audio begin");
    sample_rate_ = sample_rate;
    ESP_LOGI(kTag, "init_audio speaker codec begin");
    speaker_codec_ = bsp_audio_codec_speaker_init();
    ESP_LOGI(kTag, "init_audio speaker codec returned %p", speaker_codec_);
    if (speaker_codec_ == nullptr) {
        ESP_LOGE(kTag, "bsp_audio_codec_speaker_init failed");
        return ESP_FAIL;
    }

    esp_codec_dev_sample_info_t format = {};
    format.sample_rate = static_cast<int>(sample_rate_);
    format.channel = 2;
    format.bits_per_sample = 16;

    ESP_LOGI(kTag, "init_audio codec open begin");
    esp_err_t ret = esp_codec_dev_open(as_codec(speaker_codec_), &format);
    ESP_LOGI(kTag, "init_audio codec open returned %s", esp_err_to_name(ret));
    if (ret != ESP_OK) {
        ESP_LOGE(kTag, "esp_codec_dev_open failed: %s", esp_err_to_name(ret));
        return ret;
    }

    speaker_enabled_ = true;
    speaker_route_known_ = true;

    ret = apply_volume_curve();
    ESP_LOGI(kTag, "init_audio volume curve returned %s", esp_err_to_name(ret));
    if (ret != ESP_OK) {
        return ret;
    }

    ret = set_volume(volume);
    ESP_LOGI(kTag, "init_audio set volume returned %s", esp_err_to_name(ret));
    return ret;
}

esp_err_t Tab5BspAdapter::disable_unused_hardware()
{
    const esp_err_t ret = bsp_feature_enable(BSP_FEATURE_CAMERA, false);
    if (ret != ESP_OK) {
        ESP_LOGW(kTag, "disable camera power failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(kTag, "unused camera power disabled");
    return ESP_OK;
}

esp_err_t Tab5BspAdapter::set_volume(uint8_t volume)
{
    if (speaker_codec_ == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    return esp_codec_dev_set_out_vol(as_codec(speaker_codec_), volume > 100 ? 100 : volume);
}

esp_err_t Tab5BspAdapter::apply_volume_curve()
{
    if (speaker_codec_ == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_codec_dev_vol_curve_t curve = {};
    curve.vol_map = kAby55VolumeCurve;
    curve.count = static_cast<int>(sizeof(kAby55VolumeCurve) / sizeof(kAby55VolumeCurve[0]));
    return esp_codec_dev_set_vol_curve(as_codec(speaker_codec_), &curve);
}

esp_err_t Tab5BspAdapter::dump_codec_regs()
{
    if (speaker_codec_ == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    return esp_codec_dev_dump_reg(as_codec(speaker_codec_));
}

esp_err_t Tab5BspAdapter::read_codec_reg(uint8_t reg, int* value)
{
    if (speaker_codec_ == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    if (value == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    return esp_codec_dev_read_reg(as_codec(speaker_codec_), reg, value);
}

esp_err_t Tab5BspAdapter::set_codec_mute_safe(bool muted)
{
    if (speaker_codec_ == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    return esp_codec_dev_set_out_mute(as_codec(speaker_codec_), muted);
}

esp_err_t Tab5BspAdapter::read_headphone_inserted(bool* inserted)
{
    if (inserted == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    auto* io_expander = bsp_io_expander_init();
    if (io_expander == nullptr || io_expander->read_input_reg == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    uint32_t input = 0;
    esp_err_t ret = io_expander->read_input_reg(io_expander, &input);
    if (ret != ESP_OK) {
        return ret;
    }

    *inserted = (input & IO_EXPANDER_PIN_NUM_7) != 0;
    return ESP_OK;
}

esp_err_t Tab5BspAdapter::update_headphone_route()
{
    bool inserted = false;
    esp_err_t ret = read_headphone_inserted(&inserted);
    if (ret != ESP_OK) {
        return ret;
    }
    headphone_inserted_ = inserted;

    const bool speaker_should_enable = !inserted;
    if (speaker_route_known_ && speaker_enabled_ == speaker_should_enable) {
        return ESP_OK;
    }

    ret = bsp_feature_enable(BSP_FEATURE_SPEAKER, !inserted);
    if (ret == ESP_OK) {
        speaker_enabled_ = speaker_should_enable;
        speaker_route_known_ = true;
        ESP_LOGI(kTag, "headphone %s; speaker %s",
                 inserted ? "inserted" : "removed",
                 speaker_should_enable ? "enabled" : "muted");
    }
    return ret;
}

bool Tab5BspAdapter::headphone_inserted() const
{
    return headphone_inserted_;
}

esp_err_t Tab5BspAdapter::write_audio(const int16_t* stereo_samples, std::size_t frames)
{
    if (speaker_codec_ == nullptr || stereo_samples == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    const auto bytes = static_cast<int>(frames * 2 * sizeof(int16_t));
    return esp_codec_dev_write(as_codec(speaker_codec_), const_cast<int16_t*>(stereo_samples), bytes);
}

bool Tab5BspAdapter::lock_display(uint32_t timeout_ms)
{
    return bsp_display_lock(timeout_ms);
}

void Tab5BspAdapter::unlock_display()
{
    bsp_display_unlock();
}

lv_obj_t* Tab5BspAdapter::screen() const
{
#if LVGL_VERSION_MAJOR >= 9
    return lv_display_get_screen_active(display_);
#else
    return lv_disp_get_scr_act(display_);
#endif
}

}  // namespace tab5drum
