# Aby55

Aby55 是一个为 M5Stack Tab5 做的小型电子乐器实验：一台可以放在桌上、拿在手里、用触摸和挥动来演奏的迷你鼓机与 bassline 合成器。

它致敬的是那些经典电子乐器带来的创作方式：鼓机的直接、酸性贝斯的咬劲、桌面合成器的即时调音，以及硬件音序器那种“不用打开电脑也能做出一段 groove”的快乐。Aby55 不追求复刻任何一台机器，而是把这些精神压缩进 Tab5 的触屏、扬声器、耳机口和 IMU 传感器里，做成一个能随手玩、能继续改、也能分享给别人的开源小乐器。

This is a community project and is not affiliated with M5Stack, Espressif, or any commercial instrument brand.

## Features

- Four performance pages: `MIX`, `DRUM`, `BASS`, and `DRUM SYN`.
- Touch drum machine with 24 built-in rhythm patterns.
- Three drum kits: `Clean`, `Acid`, and `Dust`.
- Per-voice drum synth controls for pitch, decay, tone/noise, drive, and level.
- Mono bassline synth synced to the drum sequencer.
- 20 bassline styles with root selection, generation, HOLD mode, and arpeggiator toggle.
- Motion-based bassline generation from the Tab5 BMI270 accelerometer/gyroscope.
- Bass synth controls for wave, ADSR, cutoff, resonance, envelope amount, sub, drive, glide, and FX.
- Master, drum, and bass volume controls with simple H/M/L EQ boost buttons.
- Automatic speaker mute when headphones are inserted, and speaker restore when headphones are removed.
- BPM is user-controlled and is not overwritten by pattern, kit, bass style, root, or sound changes.

## Hardware

- Device: M5Stack Tab5
- MCU target: ESP32-P4
- Display path: ST7121 on the validated Tab5 hardware path
- Touch: ST712x touch firmware `1(1.80.1.16)` at I2C `0x55`
- Audio codec: ES8388 output
- Motion input: BMI270 accelerometer/gyroscope

This firmware has been tested on one Tab5 hardware baseline. Other Tab5 revisions may need display or touch adjustments.

## Quick Flash

The easiest way to try Aby55 is to download the firmware zip from the GitHub Releases page and flash it without rebuilding.

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\flash_aby55_tab5.ps1 -Port COM3
```

`COM3` is only an example/default port. Replace it with the port shown by Windows Device Manager or:

```powershell
python -m serial.tools.list_ports
```

More details are in [docs/ABY55_FLASHING.md](docs/ABY55_FLASHING.md).

## Build From Source

Requirements:

- Windows or another ESP-IDF-supported host
- ESP-IDF v5.4.2 or newer in the v5.4 line
- ESP-IDF target: `esp32p4`

Typical build:

```powershell
idf.py set-target esp32p4
idf.py build
```

This repository tracks `sdkconfig.defaults` and `dependencies.lock` for reproducible builds. The generated `sdkconfig`, `build/`, `managed_components/`, and release zips are intentionally not committed.

## Documentation

- [Current success baseline](docs/TAB5_CURRENT_SUCCESS_BASELINE.md)
- [Official display parameters used by the ST7121 path](docs/TAB5_OFFICIAL_DISPLAY_PARAMS.md)
- [Bass generator and motion mapping](docs/BASS_GENERATOR_AND_MOTION.md)
- [Flashing guide](docs/ABY55_FLASHING.md)
- [Hardware smoke test](docs/HARDWARE_TEST.md)

## Known-Good Baseline

These details are important for the current working firmware:

- Keep `CONFIG_BSP_I2C_NUM=0` in `sdkconfig.defaults`.
- Keep the local ST7121 display path and PI4IOE defaults in `components/m5stack_tab5`.
- Keep ES8388 audio streaming continuously.
- Poll headphone routing from the main app loop, not from the audio task.
- Headphone inserted: `bsp_feature_enable(BSP_FEATURE_SPEAKER, false)`.
- Headphone removed: `bsp_feature_enable(BSP_FEATURE_SPEAKER, true)`.
- Audio sample rate: `44100`.
- Audio task stack: `16384`.

## Verification

The current baseline was verified with:

- `python -m pytest tests/test_project_spec.py -v`
- `idf.py build`
- Flash to a Tab5 over USB
- Boot log reaching `tab5_drummer: app ready`
- Manual confirmation that headphone insert mutes the internal speaker and headphone removal restores it

## Do Not Regress

- Do not move away from the validated ST7121 display path without fresh hardware evidence.
- Do not change `CONFIG_BSP_I2C_NUM=0` without fresh hardware evidence.
- Do not put headphone detection in the audio task.
- Do not close/reopen the ES8388 stream on headphone insert/remove.
- Do not let rhythm or sound preset changes overwrite the current BPM.

## License

Aby55 is released under the Apache License 2.0. See [LICENSE](LICENSE) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
