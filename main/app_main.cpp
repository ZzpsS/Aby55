#include <array>
#include <algorithm>

#include "audio_engine.hpp"
#include "bmi270.h"
#include "bsp/display.h"
#include "bsp/m5stack_tab5.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tab5_bsp_adapter.hpp"
#include "ui_controller.hpp"

namespace {

constexpr uint32_t kSampleRate = 44100;
constexpr std::size_t kAudioFrames = 512;
constexpr uint32_t kAudioBlockBudgetUs = static_cast<uint32_t>((kAudioFrames * 1000000ULL) / kSampleRate);
constexpr UBaseType_t kAudioTaskPriority = configMAX_PRIORITIES - 1;
constexpr uint32_t kHeadphoneRoutePollMs = 250;
const char* const kTag = "tab5_drummer";
constexpr bool kDirectPanelTest = false;

tab5drum::PatternCatalog g_catalog;
tab5drum::AudioEngine g_audio(kSampleRate);
tab5drum::Tab5BspAdapter g_bsp;
tab5drum::UiController g_ui(g_catalog, g_audio);
bmi270_handle_t* g_imu = nullptr;

void audio_task(void*)
{
    std::array<int16_t, kAudioFrames * 2> buffer = {};

    while (true) {
        const int64_t block_start_us = esp_timer_get_time();
        g_audio.render_stereo_i16(buffer.data(), kAudioFrames);
        const int64_t render_done_us = esp_timer_get_time();
        const esp_err_t ret = g_bsp.write_audio(buffer.data(), kAudioFrames);
        const int64_t write_done_us = esp_timer_get_time();
        g_audio.record_audio_timing(
            static_cast<uint32_t>(std::max<int64_t>(0, render_done_us - block_start_us)),
            static_cast<uint32_t>(std::max<int64_t>(0, write_done_us - render_done_us)),
            kAudioBlockBudgetUs);
        if (ret != ESP_OK) {
            ESP_LOGW(kTag, "audio write failed: %s", esp_err_to_name(ret));
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }
}

void direct_panel_test()
{
    ESP_LOGI(kTag, "direct panel test begin");
    esp_lcd_panel_handle_t panel = nullptr;
    esp_lcd_panel_io_handle_t io = nullptr;
    const bsp_display_config_t cfg = {
        .dsi_bus = {
            .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
            .lane_bit_rate_mbps = 965,
        },
    };

    ESP_ERROR_CHECK(bsp_display_new(&cfg, &panel, &io));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));
    ESP_ERROR_CHECK(bsp_display_backlight_on());

    ESP_LOGI(kTag, "direct panel test drawing large color blocks");
    constexpr int kStripeHeight = 160;
    auto* pixels = static_cast<uint16_t*>(
        heap_caps_malloc(BSP_LCD_H_RES * kStripeHeight * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    if (pixels == nullptr) {
        pixels = static_cast<uint16_t*>(heap_caps_malloc(BSP_LCD_H_RES * kStripeHeight * sizeof(uint16_t), MALLOC_CAP_DMA));
    }
    ESP_ERROR_CHECK(pixels == nullptr ? ESP_ERR_NO_MEM : ESP_OK);

    const uint16_t colors[6] = {
        0xF800,  // red
        0x07E0,  // green
        0x001F,  // blue
        0xFFE0,  // yellow
        0xF81F,  // magenta
        0xFFFF,  // white
    };
    int phase = 0;
    while (true) {
        ESP_LOGI(kTag, "large block redraw phase %d", phase);
        for (int y = 0; y < BSP_LCD_V_RES; y += kStripeHeight) {
            const int h = std::min(kStripeHeight, BSP_LCD_V_RES - y);
            const uint16_t color = colors[((y / kStripeHeight) + phase) % 6];
            std::fill(pixels, pixels + BSP_LCD_H_RES * h, color);
            ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel, 0, y, BSP_LCD_H_RES, y + h, pixels));
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        phase = (phase + 1) % 6;
        ESP_ERROR_CHECK(bsp_display_backlight_on());
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void init_motion_sensor()
{
    bmi270_driver_config_t driver = {};
    driver.addr = BMI270_I2C_ADDRESS_L;
    driver.interface = BMI270_USE_I2C;
    driver.i2c_bus = bsp_i2c_get_handle();

    esp_err_t ret = bmi270_create(&driver, &g_imu);
    if (ret != ESP_OK) {
        ESP_LOGW(kTag, "BMI270 init skipped: %s", esp_err_to_name(ret));
        g_imu = nullptr;
        return;
    }

    bmi270_config_t config = {};
    config.acce_odr = BMI270_ACC_ODR_100_HZ;
    config.acce_range = BMI270_ACC_RANGE_4_G;
    config.gyro_odr = BMI270_GYR_ODR_100_HZ;
    config.gyro_range = BMI270_GYR_RANGE_1000_DPS;
    ret = bmi270_start(g_imu, &config);
    if (ret != ESP_OK) {
        ESP_LOGW(kTag, "BMI270 start skipped: %s", esp_err_to_name(ret));
        bmi270_delete(g_imu);
        g_imu = nullptr;
        return;
    }

    ESP_LOGI(kTag, "BMI270 motion input ready");
}

void poll_motion_sensor()
{
    if (g_imu == nullptr) {
        return;
    }

    float ax = 0.0f;
    float ay = 0.0f;
    float az = 0.0f;
    float gx = 0.0f;
    float gy = 0.0f;
    float gz = 0.0f;
    if (bmi270_get_acce_data(g_imu, &ax, &ay, &az) != ESP_OK ||
        bmi270_get_gyro_data(g_imu, &gx, &gy, &gz) != ESP_OK) {
        return;
    }

    g_audio.observe_motion(ax, ay, az, gx, gy, gz, static_cast<uint64_t>(esp_timer_get_time() / 1000));
}

void poll_headphone_route()
{
    static uint64_t next_poll_ms = 0;
    const uint64_t now_ms = static_cast<uint64_t>(esp_timer_get_time() / 1000);
    if (now_ms < next_poll_ms) {
        return;
    }
    next_poll_ms = now_ms + kHeadphoneRoutePollMs;

    const esp_err_t ret = g_bsp.update_headphone_route();
    if (ret != ESP_OK) {
        ESP_LOGW(kTag, "headphone route update failed: %s", esp_err_to_name(ret));
        return;
    }
    g_audio.set_output_profile(g_bsp.headphone_inserted() ? tab5drum::OutputProfile::Headphone : tab5drum::OutputProfile::Speaker);
}

}  // namespace

extern "C" void app_main(void)
{
    if (kDirectPanelTest) {
        direct_panel_test();
        return;
    }

    if (!g_catalog.load_builtin()) {
        ESP_LOGE(kTag, "no built-in rhythm patterns loaded");
        return;
    }

    g_audio.set_pattern_catalog(&g_catalog);
    g_audio.set_volume(80);

    ESP_LOGI(kTag, "init display");
    ESP_ERROR_CHECK(g_bsp.init_display());
    ESP_ERROR_CHECK(g_bsp.disable_unused_hardware());
    ESP_LOGI(kTag, "init audio");
    ESP_ERROR_CHECK(g_bsp.init_audio(kSampleRate, 80));
    ESP_ERROR_CHECK(g_bsp.update_headphone_route());
    g_audio.set_output_profile(g_bsp.headphone_inserted() ? tab5drum::OutputProfile::Headphone : tab5drum::OutputProfile::Speaker);
    ESP_ERROR_CHECK(g_bsp.dump_codec_regs());
    init_motion_sensor();

    ESP_LOGI(kTag, "create ui");
    if (g_bsp.lock_display(1000)) {
        g_ui.create(g_bsp.screen());
        lv_refr_now(nullptr);
        g_bsp.unlock_display();
        ESP_LOGI(kTag, "ui created");
    } else {
        ESP_LOGE(kTag, "could not lock LVGL display");
        return;
    }

    ESP_LOGI(kTag, "start audio task");
    xTaskCreatePinnedToCore(audio_task, "tab5_audio", 16384, nullptr, kAudioTaskPriority, nullptr, 1);
    ESP_LOGI(kTag, "app ready");

    while (true) {
        poll_headphone_route();
        poll_motion_sensor();
        vTaskDelay(pdMS_TO_TICKS(35));
    }
}
