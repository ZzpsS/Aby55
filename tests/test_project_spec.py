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

    def test_tab5_uses_custom_8mb_app_partition(self):
        sdk_defaults = (ROOT / "sdkconfig.defaults").read_text(encoding="utf-8")
        partitions_path = ROOT / "partitions.csv"
        self.assertTrue(partitions_path.exists(), "Aby55 should use a custom partition table")
        partitions = partitions_path.read_text(encoding="utf-8")

        self.assertIn("CONFIG_PARTITION_TABLE_CUSTOM=y", sdk_defaults)
        self.assertIn('CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"', sdk_defaults)
        self.assertIn('CONFIG_PARTITION_TABLE_FILENAME="partitions.csv"', sdk_defaults)
        self.assertNotIn("CONFIG_PARTITION_TABLE_SINGLE_APP=y", sdk_defaults)
        self.assertIn("factory,  app,  factory, 0x10000, 0x800000", partitions)

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

    def test_v170_stability_and_sound_quality_dsp_guardrails(self):
        transport_header = (ROOT / "main" / "include" / "transport.hpp").read_text(encoding="utf-8")
        audio_header = (ROOT / "main" / "include" / "audio_engine.hpp").read_text(encoding="utf-8")
        audio_source = (ROOT / "main" / "audio_engine.cpp").read_text(encoding="utf-8")

        for token in [
            "enum class ControlCurve",
            "class ControlParam",
            "enum class EnvelopeStage",
            "class BassAdsr",
            "class DcBlocker",
            "class SoftLimiter",
            "class DriveStage",
            "audio_clip_count",
            "audio_peak_percent",
            "bass_env_stage",
        ]:
            self.assertIn(token, transport_header + audio_header + audio_source)

        for token in [
            "bass_adsr_.trigger_gate",
            "bass_adsr_.process()",
            "bass_adsr_.is_idle()",
            "bass_dc_blocker_.process",
            "master_limiter_.process",
            "drive_stage_.process",
            "control_param_value(BassParam::Cutoff)",
            "ControlCurve::Log",
            "ControlCurve::Exp",
            "ControlCurve::Cube",
        ]:
            self.assertIn(token, audio_header + audio_source)

        for token in [
            "kBassFxSlotCount = 6",
            "BassFxSlot::Drive",
            "BassFxSlot::Fold",
            "BassFxSlot::Crush",
            "BassFxSlot::Comb",
            "BassFxSlot::Trem",
            "std::clamp(feedback",
        ]:
            self.assertIn(token, audio_source)

        for token in [
            "kick_punch",
            "snare_shell",
            "snare_noise",
            "hat_metallic",
            "drum_bus_dc_blocker_",
            "drum_limiter_",
        ]:
            self.assertIn(token, audio_header + audio_source)

    def test_v171_audio_hot_path_avoids_expensive_transcendentals(self):
        audio_source = (ROOT / "main" / "audio_engine.cpp").read_text(encoding="utf-8")

        def body_between(start, end):
            start_index = audio_source.index(start)
            end_index = audio_source.index(end, start_index)
            return audio_source[start_index:end_index]

        control_body = body_between("float ControlParam::process()", "float ControlParam::current()")
        self.assertIn("target_mapped_", control_body)
        self.assertNotIn("map(target_.load())", control_body)

        for start, end in [
            ("float BassAdsr::process()", "bool BassAdsr::is_idle()"),
            ("float SoftLimiter::process(float sample)", "uint32_t SoftLimiter::clip_count()"),
            ("float DriveStage::process(float sample, float drive)", "AudioEngine::AudioEngine"),
        ]:
            body = body_between(start, end)
            self.assertNotIn("std::exp", body)
            self.assertNotIn("std::tanh", body)

        render_body = body_between("float AudioEngine::render_voice", "float AudioEngine::control_param_value")
        self.assertNotIn("std::sin(kTwoPi * 7120.0f", render_body)
        self.assertNotIn("std::sin(kTwoPi * 6120.0f", render_body)

        bass_body = body_between("float AudioEngine::render_bass()", "float AudioEngine::apply_mix_eq")
        self.assertNotIn("std::asin(std::sin", bass_body)
        self.assertNotIn("std::sin(kTwoPi * 7.0f", bass_body)

    def test_v172_drum_voice_hot_path_uses_lightweight_math(self):
        audio_source = (ROOT / "main" / "audio_engine.cpp").read_text(encoding="utf-8")
        start_index = audio_source.index("float AudioEngine::render_voice")
        end_index = audio_source.index("float AudioEngine::control_param_value", start_index)
        render_voice = audio_source[start_index:end_index]

        for token in [
            "fast_decay",
            "fast_sine",
            "fast_pitch_ratio",
            "tick_phase",
            "kick_punch",
            "snare_shell",
            "hat_metallic",
        ]:
            self.assertIn(token, render_voice)

        for token in [
            "std::exp",
            "std::sin",
            "std::pow",
        ]:
            self.assertNotIn(token, render_voice)

    def test_v173_audio_task_has_daisy_style_realtime_guardrails(self):
        app_source = (ROOT / "main" / "app_main.cpp").read_text(encoding="utf-8")
        transport_header = (ROOT / "main" / "include" / "transport.hpp").read_text(encoding="utf-8")
        audio_header = (ROOT / "main" / "include" / "audio_engine.hpp").read_text(encoding="utf-8")
        audio_source = (ROOT / "main" / "audio_engine.cpp").read_text(encoding="utf-8")

        for token in [
            "constexpr std::size_t kAudioFrames = 512",
            "kAudioTaskPriority = configMAX_PRIORITIES - 1",
            "kAudioBlockBudgetUs",
            "record_audio_timing",
        ]:
            self.assertIn(token, app_source + audio_header + audio_source)

        for token in [
            "audio_overrun_count",
            "audio_block_peak_us",
            "audio_load_shed",
            "audio_load_shed_blocks_",
            "kLoadShedBlocks",
            "kMaxRenderedVoices",
        ]:
            self.assertIn(token, transport_header + audio_header + audio_source)

        render_body = audio_source[
            audio_source.index("std::size_t AudioEngine::render_stereo_i16"):
            audio_source.index("void AudioEngine::trigger_step")
        ]
        for token in [
            "uint8_t rendered_voices = 0",
            "rendered_voices < kMaxRenderedVoices",
            "const bool load_shed = audio_load_shed_blocks_.load() > 0",
            "if (load_shed)",
        ]:
            self.assertIn(token, render_body)

    def test_v174_unused_tab5_hardware_is_not_started_at_runtime(self):
        app_source = (ROOT / "main" / "app_main.cpp").read_text(encoding="utf-8")
        app_cmake = (ROOT / "main" / "CMakeLists.txt").read_text(encoding="utf-8")
        adapter_source = (ROOT / "components" / "tab5_bsp_adapter" / "tab5_bsp_adapter.cpp").read_text(encoding="utf-8")
        adapter_header = (ROOT / "components" / "tab5_bsp_adapter" / "include" / "tab5_bsp_adapter.hpp").read_text(encoding="utf-8")

        for token in [
            "bsp_camera_start",
            "bsp_usb_host_start",
            "bsp_sdcard_mount",
            "bsp_spiffs_mount",
            "bsp_audio_codec_microphone_init",
            "esp_wifi_init",
            "esp_bt_controller_init",
            "nvs_flash_init",
        ]:
            self.assertNotIn(token, app_source + adapter_source)

        self.assertNotIn("nvs_flash", app_cmake)
        self.assertIn("g_bsp.disable_unused_hardware()", app_source)
        self.assertIn("disable_unused_hardware", adapter_header + adapter_source)
        self.assertIn("BSP_FEATURE_CAMERA, false", adapter_source)

    def test_v180_es8388_output_profiles_and_codec_observability(self):
        app_source = (ROOT / "main" / "app_main.cpp").read_text(encoding="utf-8")
        transport_header = (ROOT / "main" / "include" / "transport.hpp").read_text(encoding="utf-8")
        audio_header = (ROOT / "main" / "include" / "audio_engine.hpp").read_text(encoding="utf-8")
        audio_source = (ROOT / "main" / "audio_engine.cpp").read_text(encoding="utf-8")
        adapter_header = (ROOT / "components" / "tab5_bsp_adapter" / "include" / "tab5_bsp_adapter.hpp").read_text(encoding="utf-8")
        adapter_source = (ROOT / "components" / "tab5_bsp_adapter" / "tab5_bsp_adapter.cpp").read_text(encoding="utf-8")

        for token in [
            "enum class OutputProfile",
            "OutputProfile::Speaker",
            "OutputProfile::Headphone",
            "output_profile",
            "set_output_profile",
            "speaker_bass_trim_",
            "speaker_low_trim_",
            "limiter_gain_reduction_percent",
        ]:
            self.assertIn(token, transport_header + audio_header + audio_source)

        for token in [
            "dump_codec_regs",
            "read_codec_reg",
            "set_codec_mute_safe",
            "apply_volume_curve",
            "headphone_inserted() const",
            "esp_codec_dev_dump_reg",
            "esp_codec_dev_read_reg",
            "esp_codec_dev_set_out_mute",
            "esp_codec_dev_set_vol_curve",
        ]:
            self.assertIn(token, adapter_header + adapter_source)

        for token in [
            "g_audio.set_output_profile",
            "g_bsp.headphone_inserted() ? tab5drum::OutputProfile::Headphone : tab5drum::OutputProfile::Speaker",
            "g_bsp.dump_codec_regs()",
        ]:
            self.assertIn(token, app_source)

        audio_task_body = app_source[app_source.index("void audio_task"):app_source.index("void direct_panel_test")]
        for token in [
            "dump_codec_regs",
            "read_codec_reg",
            "set_codec_mute_safe",
            "esp_codec_dev_write_reg",
            "esp_codec_dev_read_reg",
        ]:
            self.assertNotIn(token, audio_task_body)

    def test_v181_mix_eq_boosts_are_audible_but_bounded(self):
        audio_source = (ROOT / "main" / "audio_engine.cpp").read_text(encoding="utf-8")

        for token in [
            "constexpr float kMixEqLowBoost = 0.62f",
            "constexpr float kMixEqMidBoost = 0.72f",
            "constexpr float kMixEqHighBoost = 0.58f",
            "boosted += low_band * kMixEqLowBoost",
            "boosted += mid_band * kMixEqMidBoost",
            "boosted += high_band * kMixEqHighBoost",
            "return std::clamp(boosted, -1.40f, 1.40f)",
        ]:
            self.assertIn(token, audio_source)

        for token in [
            "boosted += low_band * 0.18f",
            "boosted += mid_band * 0.20f",
            "boosted += high_band * 0.16f",
            "constexpr float kMixEqLowBoost = 0.34f",
            "constexpr float kMixEqMidBoost = 0.40f",
            "constexpr float kMixEqHighBoost = 0.32f",
        ]:
            self.assertNotIn(token, audio_source)

    def test_v182_mix_eq_tracks_signal_while_bypassed_and_uses_broader_high_band(self):
        audio_source = (ROOT / "main" / "audio_engine.cpp").read_text(encoding="utf-8")
        start = audio_source.index("float AudioEngine::apply_mix_eq")
        end = audio_source.index("const Pattern* AudioEngine::current_pattern", start)
        body = audio_source[start:end]

        self.assertIn("const float high_band = sample - mid", body)
        self.assertIn("if (mask == 0) {\n        return sample;\n    }", body)
        self.assertLess(body.index("low += (sample - low)"), body.index("if (mask == 0)"))
        self.assertLess(body.index("const float high_band = sample - mid"), body.index("if (mask == 0)"))
        self.assertNotIn("const float high_band = sample - high", body)

    def test_v183_mix_eq_is_not_bypassed_by_audio_load_shed(self):
        audio_source = (ROOT / "main" / "audio_engine.cpp").read_text(encoding="utf-8")
        start = audio_source.index("std::size_t AudioEngine::render_stereo_i16")
        end = audio_source.index("void AudioEngine::trigger_step", start)
        body = audio_source[start:end]

        for token in [
            "drums = apply_mix_eq(drums, MixEqBus::Drum, drum_eq_mask_.load())",
            "float bass_sample = apply_mix_eq(render_bass(), MixEqBus::Bass, bass_eq_mask_.load())",
            "sample = apply_mix_eq(sample, MixEqBus::Master, master_eq_mask_.load())",
        ]:
            self.assertIn(token, body)

        for token in [
            "if (!load_shed) {\n            drums = apply_mix_eq",
            "load_shed ? render_bass()\n                                          : apply_mix_eq",
            "if (!load_shed) {\n            sample = apply_mix_eq",
        ]:
            self.assertNotIn(token, body)


if __name__ == "__main__":
    unittest.main()
