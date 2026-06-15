# Tab5 Current Success Baseline

Date: 2026-06-13
Project: `Aby55`
Firmware: `tab5_drummer`
Target: ESP32-P4 / M5Stack Tab5

This file records the current known-good settings that have been built, flashed,
booted, and manually confirmed on a Tab5 test device.

## Display And Touch

- Keep the official ST7121 display path for this Tab5.
- Do not return to blind ST7123 or ILI9881C experiments unless the hardware or
  official BSP materially changes.
- Known-good probe result:
  - `Board probe GT911 0x14: ESP_ERR_NOT_FOUND`
  - `Board probe ST712x 0x55: ESP_OK`
  - `Discovered board version 3 (LCD ST7121, Touch ST712x FW 1)`
  - `ST7123: Firmware version: 1(1.80.1.16), Max.X: 720, Max.Y: 1280`
  - `Touch panel create success`
  - `tab5_drummer: app ready`
- Keep `CONFIG_BSP_I2C_NUM=0` in both `sdkconfig` and `sdkconfig.defaults`.
- Preserve the local BSP display patch in
  `components/m5stack_tab5/src/bsp_display.c`.
- Preserve the local BSP IO expander defaults in
  `components/m5stack_tab5/src/bsp_io_expander.c`.
- Official ST7121 display parameters are documented in
  `docs/TAB5_OFFICIAL_DISPLAY_PARAMS.md`.

## Audio

- Audio output uses ES8388 through `bsp_audio_codec_speaker_init()`.
- App sample rate is `44100`.
- Audio task stack is `16384`.
- Audio task writes continuous stereo buffers through
  `Tab5BspAdapter::write_audio()`.
- Master volume is initialized to `80`.
- Current app mixer has separate master, drum, and bass volume paths.

## Headphone And Speaker Routing

- This has been manually confirmed on a Tab5 test device.
- Headphone inserted:
  - Detect with PI4IOE1 input register `IN_STA` bit 7.
  - Current implementation reads the managed IO expander input register and
    tests `IO_EXPANDER_PIN_NUM_7`.
  - Speaker is muted with `bsp_feature_enable(BSP_FEATURE_SPEAKER, false)`.
  - ES8388 audio stream keeps running so headphone output continues.
- Headphone removed:
  - Speaker is restored with `bsp_feature_enable(BSP_FEATURE_SPEAKER, true)`.
- Polling is in the main app loop, not the audio task.
- Current route poll interval is `kHeadphoneRoutePollMs = 250`.
- Relevant files:
  - `components/tab5_bsp_adapter/include/tab5_bsp_adapter.hpp`
  - `components/tab5_bsp_adapter/tab5_bsp_adapter.cpp`
  - `main/app_main.cpp`

## Current UI And Sequencer State

- UI title is `Aby55`.
- Page structure is `MIX`, `DRUM`, `BASS`, `DRUM SYN`.
- `START/STOP` is the large bottom control.
- `GEN` toggles bass generation mode:
  - `GEN`: motion/generation is enabled.
  - `HOLD`: generation/capture/queued changes are cancelled and the current
    bassline loops.
- BPM must not change automatically when switching rhythm pattern, bass style,
  root, kit, or sound parameters.
- MIX page has vertical master, drum, and bass sliders.
- MIX page has H/M/L round EQ boost buttons per master/drum/bass bus.
- DRUM page has its own `Drum Vol` slider.
- BASS page has bass volume, generator controls, compact ADSR/filter sliders,
  wave/effect controls, and bass preview.

## Build And Flash

Known-good local test environment:

- ESP-IDF: v5.4.2
- IDF tools: installed through the standard ESP-IDF tooling
- Python: ESP-IDF Python environment or any Python that can run `idf.py`
- Target: `esp32p4`
- Port used during successful flashes: `COM3`

Successful verification on 2026-06-13:

- `tests/test_project_spec.py -v`: `16 tests OK`
- `idf.py build`: success
- esptool flash on `COM3`: success
- boot log reached `tab5_drummer: app ready`
- user confirmed headphone insert mutes the speaker and headphone removal
  restores the speaker.

## Do Not Regress

- Do not move I2C away from `CONFIG_BSP_I2C_NUM=0` without fresh evidence.
- Do not replace the official ST7121 display path.
- Do not put headphone route polling in the audio task.
- Do not stop/reopen the ES8388 stream for headphone insert/remove.
- Do not let rhythm/preset changes overwrite user BPM.
