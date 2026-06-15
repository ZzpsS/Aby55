# Aby55 Firmware Flashing Guide

This guide flashes the packaged Aby55 firmware to a M5Stack Tab5. It does not
require rebuilding the project.

## Package Contents

The release zip contains:

```text
README.md
FLASHING.md
CHECKSUMS.txt
flash_aby55_tab5.ps1
flash_aby55_tab5.cmd
firmware/
  bootloader.bin
  partition-table.bin
  tab5_drummer.bin
docs/
  TAB5_CURRENT_SUCCESS_BASELINE.md
  TAB5_OFFICIAL_DISPLAY_PARAMS.md
  BASS_GENERATOR_AND_MOTION.md
```

## Requirements

- Windows
- M5Stack Tab5 connected by USB-C
- ESP-IDF tools already installed, or Python with `esptool`
- Default example port: `COM3`

If the device appears on another port, replace `COM3` with that port.

## Flash With PowerShell

Open PowerShell inside the unpacked release folder:

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\flash_aby55_tab5.ps1 -Port COM3
```

For a different port:

```powershell
.\flash_aby55_tab5.ps1 -Port COM5
```

## Flash With CMD

Open Command Prompt inside the unpacked release folder:

```bat
flash_aby55_tab5.cmd COM3
```

If no port is supplied, the CMD script uses `COM3` as a convenient Windows default.

## Manual Esptool Command

From the unpacked release folder:

```powershell
python -m esptool --chip esp32p4 -p COM3 -b 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB 0x2000 firmware/bootloader.bin 0x8000 firmware/partition-table.bin 0x10000 firmware/tab5_drummer.bin
```

## Expected Boot Log

After flashing, the normal boot path includes:

```text
Board probe ST712x 0x55: ESP_OK
Discovered board version 3 (LCD ST7121, Touch ST712x FW 1)
Display initialized with resolution 720x1280
Touch panel create success
tab5_drummer: app ready
```

No `Guru Meditation`, `task_wdt`, `No memory`, or LVGL allocation error should
appear.

## Quick Acceptance Test

1. The screen shows the Aby55 UI.
2. Tap `START`; drums and bass should play.
3. Switch between `MIX`, `DRUM`, `BASS`, and `DRUM SYN`.
4. On `BASS`, `GEN` enables generation and `HOLD` loops the current bassline.
5. Plug in headphones: the internal speaker should mute.
6. Unplug headphones: the internal speaker should resume.
7. BPM should not change just because you switch pattern, kit, style, root, or
   sound parameters.

## Troubleshooting

- If flashing cannot connect, check the USB cable, port, and whether the device
  is visible in Device Manager.
- If the port is unknown, try:

```powershell
python -m serial.tools.list_ports
```

- If PowerShell blocks the script, run:

```powershell
Set-ExecutionPolicy -Scope Process Bypass
```

- If the screen is black or flashes briefly, restore the known-good ST7121
  baseline documented in `docs/TAB5_CURRENT_SUCCESS_BASELINE.md`.
