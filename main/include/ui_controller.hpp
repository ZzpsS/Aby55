#pragma once

#include <array>
#include <cstddef>

#include "audio_engine.hpp"
#include "lvgl.h"
#include "patterns.hpp"

namespace tab5drum {

class UiController {
public:
    UiController(PatternCatalog& catalog, AudioEngine& audio);

    void create(lv_obj_t* root);
    void refresh();

private:
    enum class ActionType : uint8_t {
        StartStop,
        Tap,
        Page,
        Pattern,
        Bpm,
        Swing,
        MasterVol,
        DrumVol,
        BassVol,
        Kit,
        Fill,
        BassToggle,
        GenBass,
        DrumPage,
        Edit,
        LaneMute,
        StepCell,
        BassStyle,
        BassRoot,
        BassParam,
        ArpToggle,
        DrumVoice,
        DrumParam,
        MixEq,
    };

    struct Action {
        UiController* self = nullptr;
        ActionType type = ActionType::StartStop;
        int a = 0;
        int b = 0;
    };

    static void on_action(lv_event_t* event);
    static void on_slider(lv_event_t* event);
    static void on_timer(lv_timer_t* timer);

    Action* bind(ActionType type, int a = 0, int b = 0);
    lv_obj_t* button(lv_obj_t* parent, const char* text, int x, int y, int w, int h, ActionType type, int a = 0, int b = 0);
    lv_obj_t* slider(lv_obj_t* parent, int x, int y, int w, int h, int min, int max, int value,
                     ActionType type, int a = 0);
    lv_obj_t* label(lv_obj_t* parent, const char* text, int x, int y, const lv_font_t* font);
    void create_page_root(lv_obj_t* root);
    void rebuild_page();
    void reset_page_refs();
    void create_mix_page();
    void create_drum_page();
    void create_bass_page();
    void create_drum_synth_page();
    void refresh_nav(const TransportState& state);
    void refresh_mix(const TransportState& state);
    void refresh_drum(const TransportState& state);
    void refresh_bass(const TransportState& state);
    void refresh_drum_synth(const TransportState& state);
    void refresh_gen_button(const TransportState& state);
    void refresh_mix_eq_buttons(const TransportState& state);
    void refresh_adsr_sliders(const TransportState& state);
    void refresh_filter_sliders(const TransportState& state);
    void refresh_pattern_label(const TransportState& state);
    void refresh_drum_grid(const TransportState& state, bool force);
    void refresh_bass_preview(const TransportState& state, bool force);

    PatternCatalog& catalog_;
    AudioEngine& audio_;
    lv_obj_t* root_ = nullptr;
    lv_obj_t* page_root_ = nullptr;
    lv_obj_t* title_label_ = nullptr;
    lv_obj_t* start_button_ = nullptr;
    lv_obj_t* start_label_ = nullptr;
    lv_obj_t* bpm_label_ = nullptr;
    lv_obj_t* bpm_slider_ = nullptr;
    lv_obj_t* status_label_ = nullptr;
    std::array<lv_obj_t*, kPageCount> nav_buttons_ = {};

    lv_obj_t* pattern_label_ = nullptr;
    lv_obj_t* kit_label_ = nullptr;
    lv_obj_t* swing_label_ = nullptr;
    lv_obj_t* swing_slider_ = nullptr;
    lv_obj_t* master_volume_label_ = nullptr;
    lv_obj_t* master_volume_slider_ = nullptr;
    lv_obj_t* drum_volume_label_ = nullptr;
    lv_obj_t* drum_volume_slider_ = nullptr;
    lv_obj_t* bass_volume_label_ = nullptr;
    lv_obj_t* bass_volume_slider_ = nullptr;
    lv_obj_t* fill_button_ = nullptr;
    lv_obj_t* bass_toggle_button_ = nullptr;
    lv_obj_t* gen_button_ = nullptr;
    lv_obj_t* arp_button_ = nullptr;
    lv_obj_t* shake_label_ = nullptr;
    std::array<std::array<lv_obj_t*, kMixEqBandCount>, kMixEqBusCount> mix_eq_buttons_ = {};

    lv_obj_t* drum_page_label_ = nullptr;
    std::array<lv_obj_t*, 2> drum_page_buttons_ = {};
    lv_obj_t* edit_button_ = nullptr;
    std::array<lv_obj_t*, kVoiceCount> lane_buttons_ = {};
    std::array<std::array<lv_obj_t*, 16>, kVoiceCount> drum_cells_ = {};

    lv_obj_t* bass_style_label_ = nullptr;
    lv_obj_t* bass_root_label_ = nullptr;
    std::array<lv_obj_t*, 16> bass_preview_cells_ = {};
    std::array<lv_obj_t*, 16> bass_gate_cells_ = {};
    std::array<lv_obj_t*, 4> adsr_sliders_ = {};
    std::array<lv_obj_t*, 3> filter_sliders_ = {};
    std::array<lv_obj_t*, kBassParamCount> bass_param_labels_ = {};
    std::array<lv_obj_t*, kBassParamCount> bass_param_sliders_ = {};

    lv_obj_t* drum_voice_label_ = nullptr;
    std::array<lv_obj_t*, kVoiceCount> drum_voice_buttons_ = {};
    std::array<lv_obj_t*, kDrumParamCount> drum_param_labels_ = {};
    std::array<lv_obj_t*, kDrumParamCount> drum_param_sliders_ = {};

    std::array<Action, 260> actions_ = {};
    std::size_t action_count_ = 0;
    std::size_t common_action_count_ = 0;
    UiPage current_page_ = UiPage::Mix;
    bool page_rebuild_pending_ = false;
    std::size_t displayed_step_ = kMaxSteps + 1;
    uint8_t displayed_drum_page_ = 255;
    uint8_t displayed_mute_mask_ = 255;
    uint8_t displayed_bass_generation_ = 0;
};

}  // namespace tab5drum
