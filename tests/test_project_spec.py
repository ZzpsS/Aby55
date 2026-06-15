import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class ProjectSpecTest(unittest.TestCase):
    def load_patterns(self):
        pattern_path = ROOT / "main" / "patterns.json"
        self.assertTrue(pattern_path.exists(), "main/patterns.json must define the built-in rhythm catalog")
        return json.loads(pattern_path.read_text(encoding="utf-8"))

    def test_catalog_has_24_named_fixed_patterns(self):
        patterns = self.load_patterns()

        self.assertGreaterEqual(len(patterns), 24)
        self.assertEqual(len({pattern["id"] for pattern in patterns}), len(patterns))
        self.assertEqual(len({pattern["name"] for pattern in patterns}), len(patterns))
        self.assertTrue(any("house" in pattern["id"] for pattern in patterns))
        self.assertTrue(any("techno" in pattern["id"] for pattern in patterns))
        self.assertTrue(any("trap" in pattern["id"] for pattern in patterns))
        self.assertTrue(any("reggaeton" in pattern["id"] for pattern in patterns))
        self.assertTrue(any("dnb" in pattern["id"] or "drum" in pattern["id"] for pattern in patterns))

    def test_patterns_use_supported_lengths_lanes_and_ranges(self):
        patterns = self.load_patterns()
        required_lanes = {"kick", "snare", "clap", "closed_hat", "open_hat", "perc"}
        seen_lanes = set()

        for pattern in patterns:
            with self.subTest(pattern=pattern["id"]):
                self.assertIn(pattern["steps"], (16, 32))
                self.assertGreaterEqual(pattern["default_bpm"], 40)
                self.assertLessEqual(pattern["default_bpm"], 240)
                self.assertGreaterEqual(pattern["default_swing"], 50)
                self.assertLessEqual(pattern["default_swing"], 75)
                self.assertTrue(pattern["lanes"])

                for lane_name, steps in pattern["lanes"].items():
                    seen_lanes.add(lane_name)
                    self.assertIn(lane_name, required_lanes)
                    self.assertEqual(len(steps), pattern["steps"])
                    self.assertLessEqual(set(steps), {0, 1, 2})

        self.assertLessEqual(required_lanes, seen_lanes)

    def test_esp_idf_project_declares_local_tab5_bsp_and_target(self):
        root_cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        component_manifest = (ROOT / "main" / "idf_component.yml").read_text(encoding="utf-8")
        sdk_defaults = (ROOT / "sdkconfig.defaults").read_text(encoding="utf-8")

        self.assertIn("project(tab5_drummer)", root_cmake)
        self.assertNotIn("espressif/m5stack_tab5", component_manifest)
        self.assertTrue((ROOT / "components" / "m5stack_tab5" / "idf_component.yml").exists())
        self.assertTrue((ROOT / "dependencies.lock").exists())
        self.assertIn('CONFIG_IDF_TARGET="esp32p4"', sdk_defaults)

    def test_expected_firmware_interfaces_are_declared(self):
        headers = "\n".join(
            path.read_text(encoding="utf-8")
            for path in [
                ROOT / "main" / "include" / "patterns.hpp",
                ROOT / "main" / "include" / "transport.hpp",
                ROOT / "main" / "include" / "audio_engine.hpp",
                ROOT / "main" / "include" / "ui_controller.hpp",
            ]
        )

        for symbol in [
            "struct Pattern",
            "struct DrumEvent",
            "struct TransportState",
            "class AudioEngine",
            "class UiController",
        ]:
            self.assertIn(symbol, headers)

    def test_core_sources_include_runtime_behaviors(self):
        source_paths = [
            ROOT / "main" / "patterns.cpp",
            ROOT / "main" / "transport.cpp",
            ROOT / "main" / "audio_engine.cpp",
            ROOT / "main" / "ui_controller.cpp",
            ROOT / "main" / "app_main.cpp",
            ROOT / "components" / "tab5_bsp_adapter" / "tab5_bsp_adapter.cpp",
        ]
        sources = "\n".join(path.read_text(encoding="utf-8") for path in source_paths)

        for token in [
            "cJSON_ParseWithLength",
            "_binary_patterns_json_start",
            "samples_for_step",
            "TapTempo::tap",
            "AudioEngine::render_stereo_i16",
            "AudioEngine::generate_bassline",
            "sample_clock_",
            "bsp_audio_codec_speaker_init",
            "esp_codec_dev_write",
            "create_bass_page",
            "create_drum_synth_page",
            "xTaskCreatePinnedToCore",
        ]:
            self.assertIn(token, sources)

    def test_v15_bass_motion_and_ui_contracts_are_declared(self):
        transport = (ROOT / "main" / "include" / "transport.hpp").read_text(encoding="utf-8")
        audio_header = (ROOT / "main" / "include" / "audio_engine.hpp").read_text(encoding="utf-8")
        audio_source = (ROOT / "main" / "audio_engine.cpp").read_text(encoding="utf-8")
        ui_source = (ROOT / "main" / "ui_controller.cpp").read_text(encoding="utf-8")

        for token in [
            "Glide",
            "Env",
            "FxSelect",
            "FxAmount",
            "arp_enabled",
            "motion_capture_bars",
            "motion_capture_active",
        ]:
            self.assertIn(token, transport)

        for token in [
            "toggle_arp",
            "set_bass_fx",
            "write_generated_bass_variant",
            "finish_motion_capture",
            "kMotionCaptureBars = 2",
        ]:
            self.assertIn(token, audio_header + audio_source)

        for token in [
            "Aby55",
            "LV_DIR_VER",
            "ARP",
            "FX",
            "NOTE",
            "GATE",
        ]:
            self.assertIn(token, ui_source)

    def test_v151_bass_motion_capture_stops_and_sound_controls_are_obvious(self):
        audio_header = (ROOT / "main" / "include" / "audio_engine.hpp").read_text(encoding="utf-8")
        audio_source = (ROOT / "main" / "audio_engine.cpp").read_text(encoding="utf-8")
        ui_source = (ROOT / "main" / "ui_controller.cpp").read_text(encoding="utf-8")

        for token in [
            "motion_capture_cooldown_steps_",
            "!playing_.load()",
            "bass_queued_.load()",
            "kMotionCooldownSteps",
        ]:
            self.assertIn(token, audio_header + audio_source)

        for token in [
            "kBassOutputGain",
            "kBassFxDriveGain",
            "decay_rate = 0.9966f + decay * 0.0031f",
            "env_push = bass_.amp_env * (0.02f + env * 0.18f)",
        ]:
            self.assertIn(token, audio_source)

        self.assertIn("CAP", ui_source)

    def test_v152_bass_gen_feedback_adsr_and_header_layout(self):
        transport = (ROOT / "main" / "include" / "transport.hpp").read_text(encoding="utf-8")
        audio_source = (ROOT / "main" / "audio_engine.cpp").read_text(encoding="utf-8")
        ui_header = (ROOT / "main" / "include" / "ui_controller.hpp").read_text(encoding="utf-8")
        ui_source = (ROOT / "main" / "ui_controller.cpp").read_text(encoding="utf-8")

        for token in [
            "Attack",
            "Sustain",
            "Release",
            "attack",
            "sustain",
            "release",
        ]:
            self.assertIn(token, transport)

        for token in [
            "const bool queued = playing_.load();",
            "bass_queued_.store(queued);",
            "write_generated_bass_variant(bass_style_.load(), bass_root_.load(), bass_seed_, std::min<uint8_t>(energy, 2), queued)",
            "attack_gain",
            "sustain_level",
            "release",
        ]:
            self.assertIn(token, audio_source)

        for token in [
            "gen_button_",
            "refresh_gen_button",
            "adsr_sliders_",
            "kAdsrNames",
            "STYLE_WIDE",
            "ROOT_SEPARATE",
        ]:
            self.assertIn(token, ui_header + ui_source)

    def test_v153_adsr_is_vertical_and_release_actually_releases(self):
        audio_header = (ROOT / "main" / "include" / "audio_engine.hpp").read_text(encoding="utf-8")
        audio_source = (ROOT / "main" / "audio_engine.cpp").read_text(encoding="utf-8")
        ui_header = (ROOT / "main" / "include" / "ui_controller.hpp").read_text(encoding="utf-8")
        ui_source = (ROOT / "main" / "ui_controller.cpp").read_text(encoding="utf-8")

        self.assertNotIn("adsr_bars_", ui_header + ui_source)
        for token in [
            "refresh_adsr_sliders",
            "kAdsrSliderHeight",
            "LV_DIR_VER",
            "BassParam::Attack",
            "BassParam::Decay",
            "BassParam::Sustain",
            "BassParam::Release",
        ]:
            self.assertIn(token, ui_header + ui_source)

        for token in [
            "gate_samples_left",
            "kBassGateFraction",
            "kReleaseFastRate",
            "kReleaseSlowRate",
            "if (bass_.gate && bass_.gate_samples_left > 0)",
            "bass_.gate = false;",
        ]:
            self.assertIn(token, audio_header + audio_source)

    def test_v154_bass_page_has_compact_adsr_filter_and_bottom_controls(self):
        audio_source = (ROOT / "main" / "audio_engine.cpp").read_text(encoding="utf-8")
        ui_header = (ROOT / "main" / "include" / "ui_controller.hpp").read_text(encoding="utf-8")
        ui_source = (ROOT / "main" / "ui_controller.cpp").read_text(encoding="utf-8")

        for token in [
            "kAdsrClusterX",
            "kAdsrClusterY",
            "kFilterBlockX",
            "kFilterBlockY",
            "kCompactParamSliderWidth",
            "refresh_filter_sliders",
            "filter_sliders_",
            "BassParam::Cutoff",
            "BassParam::Resonance",
            "BassParam::Env",
        ]:
            self.assertIn(token, ui_header + ui_source)

        for token in [
            "BassParam::Sub",
            "BassParam::Drive",
            "BassParam::Glide",
            "BassParam::FxAmount",
        ]:
            self.assertIn(token, ui_source)

        self.assertIn("BassParam::Release)].store(24)", audio_source)

    def test_v155_bass_page_avoids_duplicate_adsr_and_filter_text(self):
        ui_header = (ROOT / "main" / "include" / "ui_controller.hpp").read_text(encoding="utf-8")
        ui_source = (ROOT / "main" / "ui_controller.cpp").read_text(encoding="utf-8")

        for token in [
            "adsr_label_",
            "filter_label_",
            "adsr_value_labels_",
            "filter_value_labels_",
            '"ADSR"',
            '"FILTER"',
        ]:
            self.assertNotIn(token, ui_header + ui_source)

        for token in [
            'const char* const kAdsrNames[4] = {"A", "D", "S", "R"}',
            'const char* const filter_names[3] = {"Cut", "Res", "Env"}',
            "adsr_sliders_[i] = slider",
            "filter_sliders_[i] = slider",
        ]:
            self.assertIn(token, ui_source)

    def test_v156_mix_page_has_three_band_eq_boost_switches(self):
        audio_header = (ROOT / "main" / "include" / "audio_engine.hpp").read_text(encoding="utf-8")
        audio_source = (ROOT / "main" / "audio_engine.cpp").read_text(encoding="utf-8")
        transport_header = (ROOT / "main" / "include" / "transport.hpp").read_text(encoding="utf-8")
        ui_header = (ROOT / "main" / "include" / "ui_controller.hpp").read_text(encoding="utf-8")
        ui_source = (ROOT / "main" / "ui_controller.cpp").read_text(encoding="utf-8")

        for token in [
            "enum class MixEqBus",
            "enum class MixEqBand",
            "kMixEqBusCount",
            "kMixEqBandCount",
            "master_eq_mask",
            "drum_eq_mask",
            "bass_eq_mask",
            "toggle_mix_eq",
            "mix_eq_buttons_",
            "refresh_mix_eq_buttons",
            "ActionType::MixEq",
            "kEqBandNames",
            "LOW",
            "MID",
            "HIGH",
        ]:
            self.assertIn(token, transport_header + audio_header + audio_source + ui_header + ui_source)

        for token in [
            "apply_mix_eq(drums, MixEqBus::Drum",
            "apply_mix_eq(render_bass(), MixEqBus::Bass",
            "apply_mix_eq(sample, MixEqBus::Master",
            "mix_eq_low_",
            "mix_eq_mid_",
            "mix_eq_high_",
        ]:
            self.assertIn(token, audio_header + audio_source)

    def test_v157_mix_eq_buttons_are_vertical_round_hml_columns(self):
        ui_source = (ROOT / "main" / "ui_controller.cpp").read_text(encoding="utf-8")

        for token in [
            'kEqButtonText[kMixEqBandCount] = {"H", "M", "L"}',
            "kEqButtonBandOrder[kMixEqBandCount] = {MixEqBand::High, MixEqBand::Mid, MixEqBand::Low}",
            "kEqButtonSize",
            "kEqColumnStep",
            "kEqColumnY",
            "LV_RADIUS_CIRCLE",
            "static_cast<int>(slot) * kEqColumnStep",
            "static_cast<int>(kEqButtonBandOrder[slot])",
        ]:
            self.assertIn(token, ui_source)

        self.assertNotIn('kEqButtonText[kMixEqBandCount] = {"L", "M", "H"}', ui_source)
        self.assertNotIn("static_cast<int>(band) * 54, eq_y, 46, 38", ui_source)

    def test_v158_pattern_and_sound_changes_do_not_auto_change_bpm(self):
        audio_source = (ROOT / "main" / "audio_engine.cpp").read_text(encoding="utf-8")

        self.assertIn("void AudioEngine::set_bpm(uint16_t bpm)", audio_source)
        self.assertIn("void AudioEngine::tap_tempo(uint64_t now_ms)", audio_source)
        self.assertIn("generate_bassline(1);", audio_source)
        self.assertNotIn("bpm_.store(clamp_bpm(pattern->default_bpm));", audio_source)
        self.assertNotIn("pattern->default_bpm", audio_source)

    def test_v159_gen_hold_bottom_start_spaced_eq_and_drum_volume(self):
        transport_header = (ROOT / "main" / "include" / "transport.hpp").read_text(encoding="utf-8")
        audio_header = (ROOT / "main" / "include" / "audio_engine.hpp").read_text(encoding="utf-8")
        audio_source = (ROOT / "main" / "audio_engine.cpp").read_text(encoding="utf-8")
        ui_source = (ROOT / "main" / "ui_controller.cpp").read_text(encoding="utf-8")

        for token in [
            "bass_generation_enabled",
            "toggle_bass_generation",
            "bass_generation_enabled_",
            "if (!bass_generation_enabled_.load())",
            "motion_capture_active_.store(false);",
            "bass_queued_.store(false);",
            "HOLD",
        ]:
            self.assertIn(token, transport_header + audio_header + audio_source + ui_source)

        for token in [
            "kStartButtonX",
            "kStartButtonY",
            "kStartButtonWidth",
            "kStartButtonHeight",
            'button(root_, "START", kStartButtonX, kStartButtonY, kStartButtonWidth, kStartButtonHeight',
            "lv_obj_set_style_border_width(start_button_, 2, 0)",
            "kPageRootHeight",
        ]:
            self.assertIn(token, ui_source)

        for token in [
            "kEqToSliderGap = kEqButtonSize",
            "kEqColumnY = kMixerSliderY + kMixerSliderHeight + kEqToSliderGap",
        ]:
            self.assertIn(token, ui_source)

        for token in [
            "create_drum_page",
            "drum_volume_label_ = label(page_root_, \"\", 16, 118",
            "drum_volume_slider_ = slider(page_root_, 190, 122, 470, 34",
            "lv_label_set_text_fmt(drum_volume_label_, \"Drum Vol %u\", state.drum_volume)",
        ]:
            self.assertIn(token, ui_source)

    def test_v160_headphone_insert_mutes_speaker_amp(self):
        adapter_header = (ROOT / "components" / "tab5_bsp_adapter" / "include" / "tab5_bsp_adapter.hpp").read_text(encoding="utf-8")
        adapter_source = (ROOT / "components" / "tab5_bsp_adapter" / "tab5_bsp_adapter.cpp").read_text(encoding="utf-8")
        app_source = (ROOT / "main" / "app_main.cpp").read_text(encoding="utf-8")

        for token in [
            "read_headphone_inserted",
            "update_headphone_route",
            "bsp_io_expander_init()",
            "read_input_reg",
            "IO_EXPANDER_PIN_NUM_7",
            "bsp_feature_enable(BSP_FEATURE_SPEAKER, !inserted)",
        ]:
            self.assertIn(token, adapter_header + adapter_source)

        for token in [
            "kHeadphoneRoutePollMs",
            "poll_headphone_route",
            "g_bsp.update_headphone_route()",
        ]:
            self.assertIn(token, app_source)

        self.assertNotIn("update_headphone_route();\n        g_audio.render_stereo_i16", app_source)


if __name__ == "__main__":
    unittest.main()
