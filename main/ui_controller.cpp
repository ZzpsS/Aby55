#include "ui_controller.hpp"

#include <algorithm>
#include <cstdio>

#include "esp_log.h"
#include "esp_timer.h"

namespace tab5drum {
namespace {

#if LVGL_VERSION_MAJOR >= 9
#define TAB5_BUTTON_CREATE(parent) lv_button_create(parent)
#else
#define TAB5_BUTTON_CREATE(parent) lv_btn_create(parent)
#endif

const char* const kPageNames[kPageCount] = {"MIX", "DRUM", "BASS", "DRUM SYN"};
const char* const kKitNames[3] = {"Clean", "Acid", "Dust"};
const char* const kWaveNames[4] = {"SAW", "SQR", "ACID", "DIGI"};
const char* const kFxNames[4] = {"OFF", "DIST", "DLY", "CRSH"};
const char* const kEqBandNames[kMixEqBandCount] = {"LOW", "MID", "HIGH"};
const char* const kEqButtonText[kMixEqBandCount] = {"H", "M", "L"};
const MixEqBand kEqButtonBandOrder[kMixEqBandCount] = {MixEqBand::High, MixEqBand::Mid, MixEqBand::Low};
const char* const kBassParamNames[kBassParamCount] = {
    "Wave", "Cutoff", "Reso", "Decay", "Drive", "Sub", "Glide", "Env", "FX", "FX Amt", "Attack", "Sustain", "Release",
};
const char* const kAdsrNames[4] = {"A", "D", "S", "R"};
const char* const kDrumParamNames[kDrumParamCount] = {"Pitch", "Decay", "Tone", "Drive", "Level"};
const char* const kVoiceShortNames[kVoiceCount] = {"Kick", "Snare", "Clap", "CHH", "OHH", "Tom"};
const char* const kUiTag = "ui";
constexpr uint32_t kColorBg = 0x071019;
constexpr uint32_t kColorPanel = 0x0D1823;
constexpr uint32_t kColorButton = 0x0E6BA8;
constexpr uint32_t kColorButtonDim = 0x1A2631;
constexpr uint32_t kColorSelected = 0xFF6A13;
constexpr uint32_t kColorPlay = 0x22B8A0;
constexpr uint32_t kColorStop = 0xC64750;
constexpr uint32_t kColorText = 0xFFF8EC;
constexpr uint32_t kColorMutedText = 0x9FC0D6;
constexpr uint32_t kColorGridOff = 0x182331;
constexpr uint32_t kColorGridNormal = 0x1BAED9;
constexpr uint32_t kColorGridAccent = 0xF7C948;
constexpr uint32_t kColorGridGhost = 0x44A06D;
constexpr int STYLE_WIDE = 656;
constexpr int ROOT_SEPARATE = 176;
constexpr int kAdsrClusterX = 18;
constexpr int kAdsrClusterY = 394;
constexpr int kFilterBlockX = 344;
constexpr int kFilterBlockY = 394;
constexpr int kAdsrSliderHeight = 118;
constexpr int kCompactParamSliderWidth = 248;
constexpr int kStartButtonX = 16;
constexpr int kStartButtonY = 1190;
constexpr int kStartButtonWidth = 688;
constexpr int kStartButtonHeight = 74;
constexpr int kPageRootHeight = 1000;
constexpr int kMixerSliderY = 230;
constexpr int kMixerSliderHeight = 342;
constexpr int kEqButtonSize = 44;
constexpr int kEqToSliderGap = kEqButtonSize;
constexpr int kEqColumnY = kMixerSliderY + kMixerSliderHeight + kEqToSliderGap;
constexpr int kEqColumnStep = 50;
const char* const kActionNames[] = {
    "StartStop",
    "Tap",
    "Page",
    "Pattern",
    "Bpm",
    "Swing",
    "MasterVol",
    "DrumVol",
    "BassVol",
    "Kit",
    "Fill",
    "BassToggle",
    "GenBass",
    "DrumPage",
    "Edit",
    "LaneMute",
    "StepCell",
    "BassStyle",
    "BassRoot",
    "BassParam",
    "ArpToggle",
    "DrumVoice",
    "DrumParam",
    "MixEq",
};

const char* action_name(int type)
{
    constexpr std::size_t action_count = sizeof(kActionNames) / sizeof(kActionNames[0]);
    if (type < 0 || static_cast<std::size_t>(type) >= action_count) {
        return "Unknown";
    }
    return kActionNames[type];
}

const char* motion_name(uint8_t direction)
{
    switch (direction) {
    case 0:
        return "X+";
    case 1:
        return "X-";
    case 2:
        return "Y+";
    case 3:
        return "Y-";
    case 4:
        return "Z+";
    case 5:
        return "Z-";
    default:
        return "--";
    }
}

lv_color_t color_for_hit(uint8_t hit, bool playhead)
{
    if (playhead) {
        return lv_color_hex(kColorText);
    }
    if (hit == 2) {
        return lv_color_hex(kColorGridAccent);
    }
    if (hit == 1) {
        return lv_color_hex(kColorGridNormal);
    }
    return lv_color_hex(kColorGridOff);
}

}  // namespace

UiController::UiController(PatternCatalog& catalog, AudioEngine& audio) : catalog_(catalog), audio_(audio) {}

UiController::Action* UiController::bind(ActionType type, int a, int b)
{
    if (action_count_ >= actions_.size()) {
        action_count_ = common_action_count_;
    }
    auto& action = actions_[action_count_++];
    action.self = this;
    action.type = type;
    action.a = a;
    action.b = b;
    return &action;
}

lv_obj_t* UiController::label(lv_obj_t* parent, const char* text, int x, int y, const lv_font_t* font)
{
    lv_obj_t* obj = lv_label_create(parent);
    lv_label_set_text(obj, text);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_DOT);
    lv_obj_set_width(obj, std::max(80, 660 - x));
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(kColorText), 0);
    lv_obj_set_pos(obj, x, y);
    return obj;
}

lv_obj_t* UiController::button(lv_obj_t* parent, const char* text, int x, int y, int w, int h, ActionType type, int a, int b)
{
    lv_obj_t* obj = lv_obj_create(parent);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_style_radius(obj, 7, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(kColorButton), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(obj, on_action, LV_EVENT_CLICKED, bind(type, a, b));

    if (text != nullptr && text[0] != '\0') {
        lv_obj_t* text_label = lv_label_create(obj);
        lv_label_set_text(text_label, text);
        lv_obj_set_style_text_font(text_label, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(text_label, lv_color_hex(kColorText), 0);
        lv_obj_center(text_label);
    }
    return obj;
}

lv_obj_t* UiController::slider(lv_obj_t* parent, int x, int y, int w, int h, int min, int max, int value,
                               ActionType type, int a)
{
    lv_obj_t* obj = lv_slider_create(parent);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_pos(obj, x, y);
    lv_slider_set_range(obj, min, max);
    lv_slider_set_value(obj, value, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x26313C), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(obj, lv_color_hex(kColorSelected), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(obj, lv_color_hex(kColorText), LV_PART_KNOB);
    lv_obj_set_style_border_color(obj, lv_color_hex(kColorSelected), LV_PART_KNOB);
    lv_obj_set_style_border_width(obj, 2, LV_PART_KNOB);
    lv_obj_set_style_pad_all(obj, 9, LV_PART_KNOB);
    lv_obj_add_event_cb(obj, on_slider, LV_EVENT_VALUE_CHANGED, bind(type, a, 0));
    return obj;
}

void UiController::create(lv_obj_t* root)
{
    root_ = root;
    lv_obj_clean(root_);
    lv_obj_set_style_bg_color(root_, lv_color_hex(kColorBg), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(root_, lv_color_hex(kColorText), 0);

    title_label_ = label(root_, "Aby55", 16, 14, &lv_font_montserrat_24);
    start_button_ = button(root_, "START", kStartButtonX, kStartButtonY, kStartButtonWidth, kStartButtonHeight, ActionType::StartStop);
    start_label_ = lv_obj_get_child(start_button_, 0);
    lv_obj_set_style_radius(start_button_, 10, 0);
    lv_obj_set_style_border_width(start_button_, 2, 0);
    lv_obj_set_style_border_color(start_button_, lv_color_hex(kColorText), 0);
    bpm_label_ = label(root_, "BPM", 220, 14, &lv_font_montserrat_24);
    bpm_slider_ = slider(root_, 338, 18, 198, 32, kMinBpm, kMaxBpm, 120, ActionType::Bpm);
    button(root_, "TAP", 590, 8, 96, 58, ActionType::Tap);
    status_label_ = label(root_, "", 16, 72, &lv_font_montserrat_24);

    for (std::size_t i = 0; i < kPageCount; ++i) {
        nav_buttons_[i] = button(root_, kPageNames[i], 16 + static_cast<int>(i) * 172, 106, 160, 52, ActionType::Page, static_cast<int>(i));
    }

    common_action_count_ = action_count_;
    create_page_root(root_);
    lv_timer_create(on_timer, 50, this);
    refresh();
}

void UiController::create_page_root(lv_obj_t* root)
{
    page_root_ = lv_obj_create(root);
    lv_obj_set_size(page_root_, 688, kPageRootHeight);
    lv_obj_set_pos(page_root_, 16, 176);
    lv_obj_set_style_bg_color(page_root_, lv_color_hex(kColorPanel), 0);
    lv_obj_set_style_bg_opa(page_root_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(page_root_, 0, 0);
    lv_obj_set_style_radius(page_root_, 0, 0);
    lv_obj_set_style_pad_all(page_root_, 0, 0);
    lv_obj_clear_flag(page_root_, LV_OBJ_FLAG_SCROLLABLE);
    current_page_ = UiPage::Mix;
    rebuild_page();
}

void UiController::reset_page_refs()
{
    pattern_label_ = nullptr;
    kit_label_ = nullptr;
    swing_label_ = nullptr;
    swing_slider_ = nullptr;
    master_volume_label_ = nullptr;
    master_volume_slider_ = nullptr;
    drum_volume_label_ = nullptr;
    drum_volume_slider_ = nullptr;
    bass_volume_label_ = nullptr;
    bass_volume_slider_ = nullptr;
    fill_button_ = nullptr;
    bass_toggle_button_ = nullptr;
    gen_button_ = nullptr;
    arp_button_ = nullptr;
    shake_label_ = nullptr;
    for (auto& row : mix_eq_buttons_) {
        row.fill(nullptr);
    }
    drum_page_label_ = nullptr;
    drum_page_buttons_.fill(nullptr);
    edit_button_ = nullptr;
    lane_buttons_.fill(nullptr);
    for (auto& row : drum_cells_) {
        row.fill(nullptr);
    }
    bass_style_label_ = nullptr;
    bass_root_label_ = nullptr;
    bass_preview_cells_.fill(nullptr);
    bass_gate_cells_.fill(nullptr);
    adsr_sliders_.fill(nullptr);
    filter_sliders_.fill(nullptr);
    bass_param_labels_.fill(nullptr);
    bass_param_sliders_.fill(nullptr);
    drum_voice_label_ = nullptr;
    drum_voice_buttons_.fill(nullptr);
    drum_param_labels_.fill(nullptr);
    drum_param_sliders_.fill(nullptr);
    displayed_step_ = kMaxSteps + 1;
    displayed_drum_page_ = 255;
    displayed_mute_mask_ = 255;
}

void UiController::rebuild_page()
{
    if (page_root_ == nullptr) {
        return;
    }
    action_count_ = common_action_count_;
    lv_display_t* display = lv_obj_get_display(page_root_);
    lv_display_enable_invalidation(display, false);
    lv_obj_clean(page_root_);
    reset_page_refs();

    const auto state = audio_.state();
    current_page_ = state.ui_page;
    switch (state.ui_page) {
    case UiPage::Mix:
        create_mix_page();
        break;
    case UiPage::Drum:
        create_drum_page();
        break;
    case UiPage::Bass:
        create_bass_page();
        break;
    case UiPage::DrumSynth:
        create_drum_synth_page();
        break;
    }
    lv_display_enable_invalidation(display, true);
    lv_obj_invalidate(page_root_);
    refresh();
}

void UiController::create_mix_page()
{
    pattern_label_ = label(page_root_, "", 16, 18, &lv_font_montserrat_24);
    button(page_root_, "<", 16, 64, 72, 64, ActionType::Pattern, -1);
    button(page_root_, ">", 100, 64, 72, 64, ActionType::Pattern, 1);
    fill_button_ = button(page_root_, "FILL", 190, 64, 126, 64, ActionType::Fill);

    kit_label_ = label(page_root_, "", 344, 18, &lv_font_montserrat_24);
    button(page_root_, "-", 344, 64, 72, 64, ActionType::Kit, -1);
    button(page_root_, "+", 428, 64, 72, 64, ActionType::Kit, 1);
    bass_toggle_button_ = button(page_root_, "BASS", 520, 64, 76, 64, ActionType::BassToggle);
    gen_button_ = button(page_root_, "GEN", 608, 64, 64, 64, ActionType::GenBass, 1);

    // LV_DIR_VER: narrow width plus tall height makes these LVGL sliders behave as vertical mixer faders.
    swing_label_ = label(page_root_, "", 28, 166, &lv_font_montserrat_24);
    lv_obj_set_width(swing_label_, 138);
    swing_slider_ = slider(page_root_, 68, kMixerSliderY, 52, kMixerSliderHeight, kMinSwing, kMaxSwing, 50, ActionType::Swing);

    master_volume_label_ = label(page_root_, "", 194, 166, &lv_font_montserrat_24);
    lv_obj_set_width(master_volume_label_, 138);
    master_volume_slider_ = slider(page_root_, 234, kMixerSliderY, 52, kMixerSliderHeight, 0, 100, 80, ActionType::MasterVol);

    drum_volume_label_ = label(page_root_, "", 360, 166, &lv_font_montserrat_24);
    lv_obj_set_width(drum_volume_label_, 138);
    drum_volume_slider_ = slider(page_root_, 400, kMixerSliderY, 52, kMixerSliderHeight, 0, 100, 78, ActionType::DrumVol);

    bass_volume_label_ = label(page_root_, "", 526, 166, &lv_font_montserrat_24);
    lv_obj_set_width(bass_volume_label_, 138);
    bass_volume_slider_ = slider(page_root_, 566, kMixerSliderY, 52, kMixerSliderHeight, 0, 100, 34, ActionType::BassVol);

    const int eq_group_x[kMixEqBusCount] = {238, 404, 570};
    for (std::size_t bus = 0; bus < kMixEqBusCount; ++bus) {
        for (std::size_t slot = 0; slot < kMixEqBandCount; ++slot) {
            const auto band = static_cast<std::size_t>(kEqButtonBandOrder[slot]);
            auto* eq_button = button(page_root_, kEqButtonText[slot],
                                     eq_group_x[bus], kEqColumnY + static_cast<int>(slot) * kEqColumnStep,
                                     kEqButtonSize, kEqButtonSize,
                                     ActionType::MixEq, static_cast<int>(bus), static_cast<int>(kEqButtonBandOrder[slot]));
            lv_obj_set_style_radius(eq_button, LV_RADIUS_CIRCLE, 0);
            mix_eq_buttons_[bus][band] = eq_button;
        }
    }

    shake_label_ = label(page_root_, "", 16, 786, &lv_font_montserrat_24);
}

void UiController::create_drum_page()
{
    pattern_label_ = label(page_root_, "", 16, 14, &lv_font_montserrat_24);
    drum_page_label_ = label(page_root_, "", 304, 14, &lv_font_montserrat_24);
    drum_page_buttons_[0] = button(page_root_, "PAGE 1", 304, 58, 104, 52, ActionType::DrumPage, 0);
    drum_page_buttons_[1] = button(page_root_, "PAGE 2", 420, 58, 104, 52, ActionType::DrumPage, 1);
    edit_button_ = button(page_root_, "EDIT", 540, 58, 104, 52, ActionType::Edit);
    drum_volume_label_ = label(page_root_, "", 16, 118, &lv_font_montserrat_24);
    drum_volume_slider_ = slider(page_root_, 190, 122, 470, 34, 0, 100, 78, ActionType::DrumVol);

    for (std::size_t voice = 0; voice < kVoiceCount; ++voice) {
        const int y = 190 + static_cast<int>(voice) * 66;
        lane_buttons_[voice] = button(page_root_, kVoiceShortNames[voice], 8, y, 88, 50, ActionType::LaneMute, static_cast<int>(voice));
        for (std::size_t col = 0; col < 16; ++col) {
            lv_obj_t* cell = button(page_root_, "", 104 + static_cast<int>(col) * 36, y, 30, 50, ActionType::StepCell,
                                    static_cast<int>(voice), static_cast<int>(col));
            lv_obj_set_style_bg_color(cell, lv_color_hex(kColorGridOff), 0);
            drum_cells_[voice][col] = cell;
        }
    }
}

void UiController::create_bass_page()
{
    bass_style_label_ = label(page_root_, "", 16, 12, &lv_font_montserrat_24);
    lv_obj_set_width(bass_style_label_, STYLE_WIDE);
    button(page_root_, "<", 16, 58, 64, 54, ActionType::BassStyle, -1);
    button(page_root_, ">", 88, 58, 64, 54, ActionType::BassStyle, 1);
    bass_root_label_ = label(page_root_, "", ROOT_SEPARATE, 66, &lv_font_montserrat_24);
    lv_obj_set_width(bass_root_label_, 130);
    button(page_root_, "-", 312, 58, 64, 54, ActionType::BassRoot, -1);
    button(page_root_, "+", 384, 58, 64, 54, ActionType::BassRoot, 1);
    bass_toggle_button_ = button(page_root_, "BASS", 464, 58, 76, 54, ActionType::BassToggle);
    gen_button_ = button(page_root_, "GEN", 548, 58, 62, 54, ActionType::GenBass, 1);
    arp_button_ = button(page_root_, "ARP", 618, 58, 62, 54, ActionType::ArpToggle);

    bass_volume_label_ = label(page_root_, "", 16, 132, &lv_font_montserrat_24);
    bass_volume_slider_ = slider(page_root_, 230, 134, 430, 34, 0, 100, 34, ActionType::BassVol);
    shake_label_ = label(page_root_, "", 16, 178, &lv_font_montserrat_24);

    label(page_root_, "NOTE", 16, 240, &lv_font_montserrat_24);
    label(page_root_, "GATE", 16, 336, &lv_font_montserrat_24);
    for (std::size_t i = 0; i < 16; ++i) {
        bass_preview_cells_[i] = lv_obj_create(page_root_);
        lv_obj_set_size(bass_preview_cells_[i], 32, 68);
        lv_obj_set_pos(bass_preview_cells_[i], 82 + static_cast<int>(i) * 37, 236);
        lv_obj_set_style_radius(bass_preview_cells_[i], 5, 0);
        lv_obj_set_style_border_width(bass_preview_cells_[i], 0, 0);
        lv_obj_set_style_bg_color(bass_preview_cells_[i], lv_color_hex(kColorGridOff), 0);
        lv_obj_set_style_bg_opa(bass_preview_cells_[i], LV_OPA_COVER, 0);

        bass_gate_cells_[i] = lv_obj_create(page_root_);
        lv_obj_set_size(bass_gate_cells_[i], 32, 32);
        lv_obj_set_pos(bass_gate_cells_[i], 82 + static_cast<int>(i) * 37, 332);
        lv_obj_set_style_radius(bass_gate_cells_[i], 5, 0);
        lv_obj_set_style_border_width(bass_gate_cells_[i], 0, 0);
        lv_obj_set_style_bg_color(bass_gate_cells_[i], lv_color_hex(kColorGridOff), 0);
        lv_obj_set_style_bg_opa(bass_gate_cells_[i], LV_OPA_COVER, 0);
    }

    // LV_DIR_VER: the ADSR controls are intentionally four vertical sliders only, without a separate graph.
    const BassParam adsr_params[4] = {BassParam::Attack, BassParam::Decay, BassParam::Sustain, BassParam::Release};
    const int adsr_x[4] = {
        kAdsrClusterX + 62,
        kAdsrClusterX + 112,
        kAdsrClusterX + 162,
        kAdsrClusterX + 212,
    };
    const int adsr_defaults[4] = {8, 56, 34, 24};
    for (std::size_t i = 0; i < 4; ++i) {
        auto* name = label(page_root_, kAdsrNames[i], adsr_x[i] + 4, kAdsrClusterY + 2, &lv_font_montserrat_24);
        lv_obj_set_width(name, 34);
        adsr_sliders_[i] = slider(page_root_, adsr_x[i], kAdsrClusterY + 34, 30, kAdsrSliderHeight, 0, 100, adsr_defaults[i],
                                  ActionType::BassParam, static_cast<int>(adsr_params[i]));
    }

    const BassParam filter_params[3] = {BassParam::Cutoff, BassParam::Resonance, BassParam::Env};
    const char* const filter_names[3] = {"Cut", "Res", "Env"};
    const int filter_defaults[3] = {56, 34, 58};
    const int filter_x[3] = {kFilterBlockX + 6, kFilterBlockX + 104, kFilterBlockX + 202};
    for (std::size_t i = 0; i < 3; ++i) {
        auto* name = label(page_root_, filter_names[i], filter_x[i] - 2, kFilterBlockY + 2, &lv_font_montserrat_24);
        lv_obj_set_width(name, 66);
        filter_sliders_[i] = slider(page_root_, filter_x[i] + 8, kFilterBlockY + 34, 30, kAdsrSliderHeight, 0, 100,
                                    filter_defaults[i], ActionType::BassParam, static_cast<int>(filter_params[i]));
    }

    bass_param_labels_[static_cast<std::size_t>(BassParam::Wave)] = label(page_root_, "", 16, 566, &lv_font_montserrat_24);
    lv_obj_set_width(bass_param_labels_[static_cast<std::size_t>(BassParam::Wave)], 190);
    button(page_root_, "<", 220, 556, 58, 50, ActionType::BassParam, static_cast<int>(BassParam::Wave), -1);
    button(page_root_, ">", 290, 556, 58, 50, ActionType::BassParam, static_cast<int>(BassParam::Wave), 1);
    bass_param_labels_[static_cast<std::size_t>(BassParam::FxSelect)] = label(page_root_, "", 370, 566, &lv_font_montserrat_24);
    lv_obj_set_width(bass_param_labels_[static_cast<std::size_t>(BassParam::FxSelect)], 180);
    button(page_root_, "<", 548, 556, 58, 50, ActionType::BassParam, static_cast<int>(BassParam::FxSelect), -1);
    button(page_root_, ">", 616, 556, 58, 50, ActionType::BassParam, static_cast<int>(BassParam::FxSelect), 1);

    const BassParam left_params[2] = {BassParam::Sub, BassParam::Glide};
    const BassParam right_params[2] = {BassParam::Drive, BassParam::FxAmount};
    for (std::size_t row = 0; row < 2; ++row) {
        const int y = 642 + static_cast<int>(row) * 96;
        const auto left = static_cast<std::size_t>(left_params[row]);
        bass_param_labels_[left] = label(page_root_, "", 16, y, &lv_font_montserrat_24);
        lv_obj_set_width(bass_param_labels_[left], kCompactParamSliderWidth);
        bass_param_sliders_[left] = slider(page_root_, 16, y + 34, kCompactParamSliderWidth, 28, 0, 100, 50,
                                           ActionType::BassParam, static_cast<int>(left_params[row]));

        const auto right = static_cast<std::size_t>(right_params[row]);
        bass_param_labels_[right] = label(page_root_, "", 370, y, &lv_font_montserrat_24);
        lv_obj_set_width(bass_param_labels_[right], kCompactParamSliderWidth);
        bass_param_sliders_[right] = slider(page_root_, 370, y + 34, kCompactParamSliderWidth, 28, 0, 100, 50,
                                            ActionType::BassParam, static_cast<int>(right_params[row]));
    }
}

void UiController::create_drum_synth_page()
{
    drum_voice_label_ = label(page_root_, "", 16, 18, &lv_font_montserrat_24);
    for (std::size_t voice = 0; voice < kVoiceCount; ++voice) {
        const int col = static_cast<int>(voice % 3);
        const int row = static_cast<int>(voice / 3);
        drum_voice_buttons_[voice] = button(page_root_, kVoiceShortNames[voice], 16 + col * 224, 66 + row * 70, 200, 58,
                                            ActionType::DrumVoice, static_cast<int>(voice));
    }

    for (std::size_t param = 0; param < kDrumParamCount; ++param) {
        const int y = 244 + static_cast<int>(param) * 82;
        drum_param_labels_[param] = label(page_root_, "", 16, y, &lv_font_montserrat_24);
        drum_param_sliders_[param] = slider(page_root_, 260, y + 8, 390, 40, 0, 100, 50,
                                            ActionType::DrumParam, static_cast<int>(param));
    }
}

void UiController::on_action(lv_event_t* event)
{
    auto* action = static_cast<Action*>(lv_event_get_user_data(event));
    if (action == nullptr || action->self == nullptr) {
        return;
    }

    auto* self = action->self;
    ESP_LOGI(kUiTag, "click %s a=%d b=%d", action_name(static_cast<int>(action->type)), action->a, action->b);
    switch (action->type) {
    case ActionType::StartStop: {
        const auto state = self->audio_.state();
        self->audio_.set_playing(!state.playing);
        break;
    }
    case ActionType::Tap:
        self->audio_.tap_tempo(static_cast<uint64_t>(esp_timer_get_time() / 1000));
        break;
    case ActionType::Page:
        self->audio_.set_ui_page(static_cast<UiPage>(action->a));
        self->page_rebuild_pending_ = true;
        return;
    case ActionType::Pattern: {
        if (!self->catalog_.empty()) {
            const auto state = self->audio_.state();
            const auto count = static_cast<int>(self->catalog_.size());
            const auto next = (static_cast<int>(state.current_pattern) + action->a + count) % count;
            self->audio_.set_pattern_index(static_cast<std::size_t>(next));
        }
        break;
    }
    case ActionType::Bpm:
        self->audio_.adjust_bpm(action->a);
        break;
    case ActionType::Swing:
        self->audio_.adjust_swing(action->a);
        break;
    case ActionType::MasterVol:
        self->audio_.adjust_volume(action->a);
        break;
    case ActionType::DrumVol:
        self->audio_.adjust_drum_volume(action->a);
        break;
    case ActionType::BassVol:
        self->audio_.adjust_bass_volume(action->a);
        break;
    case ActionType::Kit:
        self->audio_.adjust_kit(action->a);
        break;
    case ActionType::Fill:
        self->audio_.trigger_fill();
        break;
    case ActionType::BassToggle:
        self->audio_.toggle_bass_enabled();
        break;
    case ActionType::GenBass:
        self->audio_.toggle_bass_generation();
        break;
    case ActionType::DrumPage:
        self->audio_.set_drum_page(static_cast<uint8_t>(action->a));
        self->displayed_step_ = kMaxSteps + 1;
        break;
    case ActionType::Edit:
        self->audio_.toggle_edit_mode();
        break;
    case ActionType::LaneMute:
        self->audio_.toggle_lane_mute(static_cast<DrumVoice>(action->a));
        break;
    case ActionType::StepCell: {
        const auto state = self->audio_.state();
        const auto step = static_cast<std::size_t>(state.drum_page * 16 + action->b);
        self->audio_.toggle_drum_cell(static_cast<DrumVoice>(action->a), step);
        self->displayed_step_ = kMaxSteps + 1;
        break;
    }
    case ActionType::BassStyle:
        self->audio_.adjust_bass_style(action->a);
        break;
    case ActionType::BassRoot:
        self->audio_.adjust_bass_root(action->a);
        break;
    case ActionType::BassParam:
        if (static_cast<BassParam>(action->a) == BassParam::Wave ||
            static_cast<BassParam>(action->a) == BassParam::FxSelect) {
            const auto state = self->audio_.state();
            const auto param = static_cast<BassParam>(action->a);
            const int current = param == BassParam::Wave ? state.bass_params.wave : state.bass_params.fx_select;
            const int next = (current + action->b + 4) % 4;
            self->audio_.set_bass_param(param, static_cast<uint8_t>(next));
        } else {
            self->audio_.adjust_bass_param(static_cast<BassParam>(action->a), action->b);
        }
        break;
    case ActionType::ArpToggle:
        self->audio_.toggle_arp();
        break;
    case ActionType::DrumVoice:
        self->audio_.set_selected_drum_voice(static_cast<DrumVoice>(action->a));
        break;
    case ActionType::DrumParam:
        self->audio_.adjust_drum_param(static_cast<DrumParam>(action->a), action->b);
        break;
    case ActionType::MixEq:
        self->audio_.toggle_mix_eq(static_cast<MixEqBus>(action->a), static_cast<MixEqBand>(action->b));
        break;
    }
    self->refresh();
}

void UiController::on_slider(lv_event_t* event)
{
    auto* action = static_cast<Action*>(lv_event_get_user_data(event));
    if (action == nullptr || action->self == nullptr) {
        return;
    }

    auto* self = action->self;
    lv_obj_t* obj = lv_event_get_target_obj(event);
    const int value = static_cast<int>(lv_slider_get_value(obj));
    switch (action->type) {
    case ActionType::Bpm:
        self->audio_.set_bpm(static_cast<uint16_t>(value));
        break;
    case ActionType::Swing:
        self->audio_.set_swing(static_cast<uint8_t>(value));
        break;
    case ActionType::MasterVol:
        self->audio_.set_volume(static_cast<uint8_t>(value));
        break;
    case ActionType::DrumVol:
        self->audio_.set_drum_volume(static_cast<uint8_t>(value));
        break;
    case ActionType::BassVol:
        self->audio_.set_bass_volume(static_cast<uint8_t>(value));
        break;
    case ActionType::BassParam:
        self->audio_.set_bass_param(static_cast<BassParam>(action->a), static_cast<uint8_t>(value));
        break;
    case ActionType::DrumParam:
        self->audio_.set_drum_param(static_cast<DrumParam>(action->a), static_cast<uint8_t>(value));
        break;
    default:
        break;
    }
    self->refresh();
}

void UiController::on_timer(lv_timer_t* timer)
{
    auto* self = static_cast<UiController*>(lv_timer_get_user_data(timer));
    if (self != nullptr) {
        const auto state = self->audio_.state();
        if (self->page_rebuild_pending_ || state.ui_page != self->current_page_) {
            self->page_rebuild_pending_ = false;
            self->rebuild_page();
            return;
        }
        self->refresh();
    }
}

void UiController::refresh()
{
    const auto state = audio_.state();
    if (state.ui_page != current_page_) {
        page_rebuild_pending_ = true;
        return;
    }

    lv_label_set_text(start_label_, state.playing ? "STOP" : "START");
    lv_obj_set_style_bg_color(start_button_, state.playing ? lv_color_hex(kColorStop) : lv_color_hex(kColorPlay), 0);
    lv_label_set_text_fmt(bpm_label_, "BPM %u", state.bpm);
    if (bpm_slider_ != nullptr) {
        lv_slider_set_value(bpm_slider_, state.bpm, LV_ANIM_OFF);
    }
    lv_label_set_text_fmt(status_label_, "%s  %s  Bass %s%s%s%s",
                          kKitNames[state.kit % 3],
                          state.edit_mode ? "EDIT" : "PLAY",
                          state.bass_enabled ? "ON" : "OFF",
                          state.arp_enabled ? " ARP" : "",
                          state.bass_generation_enabled ? "" : " HOLD",
                          state.bass_queued ? " ->" : "");

    refresh_nav(state);
    switch (state.ui_page) {
    case UiPage::Mix:
        refresh_mix(state);
        break;
    case UiPage::Drum:
        refresh_drum(state);
        break;
    case UiPage::Bass:
        refresh_bass(state);
        break;
    case UiPage::DrumSynth:
        refresh_drum_synth(state);
        break;
    }
}

void UiController::refresh_nav(const TransportState& state)
{
    const auto selected = static_cast<std::size_t>(state.ui_page);
    for (std::size_t i = 0; i < nav_buttons_.size(); ++i) {
        if (nav_buttons_[i] != nullptr) {
            lv_obj_set_style_bg_color(nav_buttons_[i],
                                      i == selected ? lv_color_hex(kColorSelected) : lv_color_hex(kColorButtonDim), 0);
        }
    }
}

void UiController::refresh_gen_button(const TransportState& state)
{
    if (gen_button_ == nullptr) {
        return;
    }

    const bool active = state.motion_capture_active || state.bass_queued;
    const bool flash_on = ((esp_timer_get_time() / 250000) & 1) != 0;
    lv_obj_t* text = lv_obj_get_child(gen_button_, 0);
    if (!state.bass_generation_enabled) {
        if (text != nullptr) {
            lv_label_set_text(text, "HOLD");
        }
        lv_obj_set_style_bg_color(gen_button_, lv_color_hex(kColorButtonDim), 0);
        return;
    }
    if (text != nullptr) {
        lv_label_set_text(text, state.motion_capture_active ? "CAP" : (state.bass_queued ? "GEN>" : "GEN"));
    }
    if (active) {
        lv_obj_set_style_bg_color(gen_button_, flash_on ? lv_color_hex(kColorSelected) : lv_color_hex(kColorButtonDim), 0);
    } else {
        lv_obj_set_style_bg_color(gen_button_, lv_color_hex(kColorButton), 0);
    }
}

void UiController::refresh_mix_eq_buttons(const TransportState& state)
{
    const uint8_t masks[kMixEqBusCount] = {state.master_eq_mask, state.drum_eq_mask, state.bass_eq_mask};
    for (std::size_t bus = 0; bus < kMixEqBusCount; ++bus) {
        for (std::size_t band = 0; band < kMixEqBandCount; ++band) {
            auto* button = mix_eq_buttons_[bus][band];
            if (button == nullptr) {
                continue;
            }
            const bool enabled = (masks[bus] & (1u << band)) != 0;
            lv_obj_set_style_bg_color(button, enabled ? lv_color_hex(kColorSelected) : lv_color_hex(kColorButtonDim), 0);
            lv_obj_set_style_border_width(button, enabled ? 2 : 0, 0);
            lv_obj_set_style_border_color(button, lv_color_hex(kColorText), 0);
        }
    }
}

void UiController::refresh_adsr_sliders(const TransportState& state)
{
    if (adsr_sliders_[0] == nullptr) {
        return;
    }

    const uint8_t values[4] = {
        state.bass_params.attack,
        state.bass_params.decay,
        state.bass_params.sustain,
        state.bass_params.release,
    };
    for (std::size_t i = 0; i < adsr_sliders_.size(); ++i) {
        if (adsr_sliders_[i] != nullptr) {
            lv_slider_set_value(adsr_sliders_[i], values[i], LV_ANIM_OFF);
        }
    }
}

void UiController::refresh_filter_sliders(const TransportState& state)
{
    if (filter_sliders_[0] == nullptr) {
        return;
    }

    const uint8_t values[3] = {
        state.bass_params.cutoff,
        state.bass_params.resonance,
        state.bass_params.env,
    };
    for (std::size_t i = 0; i < filter_sliders_.size(); ++i) {
        if (filter_sliders_[i] != nullptr) {
            lv_slider_set_value(filter_sliders_[i], values[i], LV_ANIM_OFF);
        }
    }
}

void UiController::refresh_pattern_label(const TransportState& state)
{
    if (pattern_label_ == nullptr) {
        return;
    }
    if (catalog_.empty()) {
        lv_label_set_text(pattern_label_, "No Patterns");
        return;
    }
    const auto& pattern = catalog_.at(state.current_pattern);
    lv_label_set_text_fmt(pattern_label_, "%02u/%02u  %s",
                          static_cast<unsigned>((state.current_pattern % catalog_.size()) + 1),
                          static_cast<unsigned>(catalog_.size()),
                          pattern.name.c_str());
}

void UiController::refresh_mix(const TransportState& state)
{
    refresh_pattern_label(state);
    if (kit_label_ != nullptr) {
        lv_label_set_text_fmt(kit_label_, "Kit %s", kKitNames[state.kit % 3]);
    }
    if (swing_label_ != nullptr) {
        lv_label_set_text_fmt(swing_label_, "Swing %u", state.swing);
    }
    if (swing_slider_ != nullptr) {
        lv_slider_set_value(swing_slider_, state.swing, LV_ANIM_OFF);
    }
    if (master_volume_label_ != nullptr) {
        lv_label_set_text_fmt(master_volume_label_, "Master %u", state.volume);
    }
    if (master_volume_slider_ != nullptr) {
        lv_slider_set_value(master_volume_slider_, state.volume, LV_ANIM_OFF);
    }
    if (drum_volume_label_ != nullptr) {
        lv_label_set_text_fmt(drum_volume_label_, "Drum %u", state.drum_volume);
    }
    if (drum_volume_slider_ != nullptr) {
        lv_slider_set_value(drum_volume_slider_, state.drum_volume, LV_ANIM_OFF);
    }
    if (bass_volume_label_ != nullptr) {
        lv_label_set_text_fmt(bass_volume_label_, "Bass %u", state.bass_volume);
    }
    if (bass_volume_slider_ != nullptr) {
        lv_slider_set_value(bass_volume_slider_, state.bass_volume, LV_ANIM_OFF);
    }
    refresh_mix_eq_buttons(state);
    if (fill_button_ != nullptr) {
        lv_obj_set_style_bg_color(fill_button_, state.fill_armed ? lv_color_hex(kColorSelected) : lv_color_hex(kColorButton), 0);
    }
    if (bass_toggle_button_ != nullptr) {
        lv_obj_set_style_bg_color(bass_toggle_button_, state.bass_enabled ? lv_color_hex(kColorPlay) : lv_color_hex(kColorButtonDim), 0);
    }
    refresh_gen_button(state);
    if (shake_label_ != nullptr) {
        const char* gen_state = !state.bass_generation_enabled ? "hold" : (state.bass_queued ? "queued" : "ready");
        if (state.imu_ready) {
            lv_label_set_text_fmt(shake_label_, "Bass %s / Root %s / Motion %s %ums E%u / CAP %u/%u / %s",
                                  audio_.bass_style_name(state.bass_style),
                                  audio_.bass_root_name(state.bass_root),
                                  motion_name(state.motion_direction),
                                  static_cast<unsigned>(state.motion_period_ms),
                                  static_cast<unsigned>(state.motion_energy),
                                  static_cast<unsigned>(state.motion_capture_bars),
                                  2u,
                                  gen_state);
        } else {
            lv_label_set_text_fmt(shake_label_, "Bass %s / Root %s / Motion off / %s",
                                  audio_.bass_style_name(state.bass_style),
                                  audio_.bass_root_name(state.bass_root),
                                  gen_state);
        }
    }
}

void UiController::refresh_drum(const TransportState& state)
{
    refresh_pattern_label(state);
    if (drum_volume_label_ != nullptr) {
        lv_label_set_text_fmt(drum_volume_label_, "Drum Vol %u", state.drum_volume);
    }
    if (drum_volume_slider_ != nullptr) {
        lv_slider_set_value(drum_volume_slider_, state.drum_volume, LV_ANIM_OFF);
    }
    if (drum_page_label_ != nullptr) {
        lv_label_set_text_fmt(drum_page_label_, "DRUM PAGE %u", static_cast<unsigned>(state.drum_page + 1));
    }
    for (std::size_t i = 0; i < drum_page_buttons_.size(); ++i) {
        if (drum_page_buttons_[i] != nullptr) {
            lv_obj_set_style_bg_color(drum_page_buttons_[i],
                                      i == state.drum_page ? lv_color_hex(kColorSelected) : lv_color_hex(kColorButtonDim), 0);
        }
    }
    if (edit_button_ != nullptr) {
        lv_label_set_text(static_cast<lv_obj_t*>(lv_obj_get_child(edit_button_, 0)), state.edit_mode ? "EDIT ON" : "EDIT");
        lv_obj_set_style_bg_color(edit_button_, state.edit_mode ? lv_color_hex(kColorSelected) : lv_color_hex(kColorButton), 0);
    }
    refresh_drum_grid(state, displayed_drum_page_ != state.drum_page || displayed_mute_mask_ != state.lane_mute_mask);
    displayed_drum_page_ = state.drum_page;
    displayed_mute_mask_ = state.lane_mute_mask;
}

void UiController::refresh_drum_grid(const TransportState& state, bool force)
{
    if (catalog_.empty() || drum_cells_[0][0] == nullptr) {
        return;
    }
    const auto& pattern = catalog_.at(state.current_pattern);
    const std::size_t base = static_cast<std::size_t>(state.drum_page) * 16;
    const std::size_t current = state.current_step % pattern.steps;
    if (!force && displayed_step_ == current) {
        return;
    }
    displayed_step_ = current;

    for (std::size_t voice = 0; voice < kVoiceCount; ++voice) {
        const bool muted = (state.lane_mute_mask & (1u << voice)) != 0;
        if (lane_buttons_[voice] != nullptr) {
            lv_obj_set_style_bg_color(lane_buttons_[voice], muted ? lv_color_hex(0x555A61) : lv_color_hex(kColorButton), 0);
        }
        for (std::size_t col = 0; col < 16; ++col) {
            const auto step = base + col;
            const bool visible = step < pattern.steps;
            auto* cell = drum_cells_[voice][col];
            if (cell == nullptr) {
                continue;
            }
            if (!visible) {
                lv_obj_add_flag(cell, LV_OBJ_FLAG_HIDDEN);
                continue;
            }
            lv_obj_clear_flag(cell, LV_OBJ_FLAG_HIDDEN);
            const bool playhead = step == current && state.playing;
            const auto hit = pattern.hit(static_cast<DrumVoice>(voice), step);
            lv_obj_set_style_bg_color(cell, color_for_hit(hit, playhead), 0);
        }
    }
}

void UiController::refresh_bass(const TransportState& state)
{
    if (bass_style_label_ != nullptr) {
        lv_label_set_text_fmt(bass_style_label_, "Style %s", audio_.bass_style_name(state.bass_style));
    }
    if (bass_root_label_ != nullptr) {
        lv_label_set_text_fmt(bass_root_label_, "Root %s", audio_.bass_root_name(state.bass_root));
    }
    if (bass_volume_label_ != nullptr) {
        lv_label_set_text_fmt(bass_volume_label_, "Bass Vol %u", state.bass_volume);
    }
    if (bass_volume_slider_ != nullptr) {
        lv_slider_set_value(bass_volume_slider_, state.bass_volume, LV_ANIM_OFF);
    }
    if (bass_toggle_button_ != nullptr) {
        lv_obj_set_style_bg_color(bass_toggle_button_, state.bass_enabled ? lv_color_hex(kColorPlay) : lv_color_hex(kColorButtonDim), 0);
    }
    if (arp_button_ != nullptr) {
        lv_obj_set_style_bg_color(arp_button_, state.arp_enabled ? lv_color_hex(kColorSelected) : lv_color_hex(kColorButtonDim), 0);
    }
    refresh_gen_button(state);
    if (shake_label_ != nullptr) {
        const char* gen_state = !state.bass_generation_enabled ? "hold" : (state.bass_queued ? "queued" : "ready");
        if (state.imu_ready) {
            lv_label_set_text_fmt(shake_label_, "Motion %s  %ums  E%u  CAP %u/%u  %s",
                                  motion_name(state.motion_direction),
                                  static_cast<unsigned>(state.motion_period_ms),
                                  static_cast<unsigned>(state.motion_energy),
                                  static_cast<unsigned>(state.motion_capture_bars),
                                  2u,
                                  gen_state);
        } else {
            lv_label_set_text_fmt(shake_label_, "Motion off  %s", gen_state);
        }
    }

    const uint8_t values[kBassParamCount] = {
        state.bass_params.wave,
        state.bass_params.cutoff,
        state.bass_params.resonance,
        state.bass_params.decay,
        state.bass_params.drive,
        state.bass_params.sub,
        state.bass_params.glide,
        state.bass_params.env,
        state.bass_params.fx_select,
        state.bass_params.fx_amount,
        state.bass_params.attack,
        state.bass_params.sustain,
        state.bass_params.release,
    };
    for (std::size_t i = 0; i < kBassParamCount; ++i) {
        if (bass_param_labels_[i] == nullptr) {
            continue;
        }
        if (i == static_cast<std::size_t>(BassParam::Wave)) {
            lv_label_set_text_fmt(bass_param_labels_[i], "%s  %s", kBassParamNames[i], kWaveNames[values[i] % 4]);
        } else if (i == static_cast<std::size_t>(BassParam::FxSelect)) {
            lv_label_set_text_fmt(bass_param_labels_[i], "%s  %s", kBassParamNames[i], kFxNames[values[i] % 4]);
        } else {
            lv_label_set_text_fmt(bass_param_labels_[i], "%s  %u", kBassParamNames[i], values[i]);
            if (bass_param_sliders_[i] != nullptr) {
                lv_slider_set_value(bass_param_sliders_[i], values[i], LV_ANIM_OFF);
            }
        }
    }
    refresh_adsr_sliders(state);
    refresh_filter_sliders(state);
    refresh_bass_preview(state, false);
}

void UiController::refresh_bass_preview(const TransportState& state, bool force)
{
    if (bass_preview_cells_[0] == nullptr) {
        return;
    }
    const auto current = state.current_step % 16;
    if (!force && displayed_step_ == current && !state.bass_queued) {
        return;
    }
    displayed_step_ = current;

    for (std::size_t i = 0; i < 16; ++i) {
        const auto step = audio_.bass_step(i);
        const bool playhead = state.playing && i == current;
        const int pitch_bucket = step.gate ? std::clamp(static_cast<int>(step.note) - 24, 0, 36) : 0;
        const int note_height = step.gate ? 22 + (pitch_bucket * 54 / 36) : 12;
        lv_obj_set_size(bass_preview_cells_[i], 32, note_height);
        lv_obj_set_pos(bass_preview_cells_[i], 82 + static_cast<int>(i) * 37, 320 - note_height);
        lv_color_t note_color = lv_color_hex(kColorGridOff);
        if (playhead) {
            note_color = lv_color_hex(kColorText);
        } else if (step.gate) {
            note_color = lv_color_hex(step.note >= 48 ? kColorGridAccent : kColorGridNormal);
        }
        lv_obj_set_style_bg_color(bass_preview_cells_[i], note_color, 0);

        if (bass_gate_cells_[i] == nullptr) {
            continue;
        }
        lv_color_t color = lv_color_hex(kColorGridOff);
        if (playhead) {
            color = lv_color_hex(kColorText);
        } else if (step.accent) {
            color = lv_color_hex(kColorSelected);
        } else if (step.slide) {
            color = lv_color_hex(0x8F6CE0);
        } else if (step.gate) {
            color = lv_color_hex(kColorGridNormal);
        }
        lv_obj_set_style_bg_color(bass_gate_cells_[i], color, 0);
    }
}

void UiController::refresh_drum_synth(const TransportState& state)
{
    const auto selected = static_cast<std::size_t>(state.selected_drum_voice);
    if (drum_voice_label_ != nullptr) {
        lv_label_set_text_fmt(drum_voice_label_, "Voice %s", kVoiceShortNames[selected]);
    }
    for (std::size_t voice = 0; voice < kVoiceCount; ++voice) {
        if (drum_voice_buttons_[voice] != nullptr) {
            lv_obj_set_style_bg_color(drum_voice_buttons_[voice],
                                      voice == selected ? lv_color_hex(kColorSelected) : lv_color_hex(kColorButton), 0);
        }
    }

    const auto& params = state.drum_voice_params[selected];
    const uint8_t values[kDrumParamCount] = {params.pitch, params.decay, params.tone, params.drive, params.level};
    for (std::size_t i = 0; i < kDrumParamCount; ++i) {
        if (drum_param_labels_[i] != nullptr) {
            lv_label_set_text_fmt(drum_param_labels_[i], "%s  %u", kDrumParamNames[i], values[i]);
        }
        if (drum_param_sliders_[i] != nullptr) {
            lv_slider_set_value(drum_param_sliders_[i], values[i], LV_ANIM_OFF);
        }
    }
}

}  // namespace tab5drum
