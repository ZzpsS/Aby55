# Aby55 Hardware Test

Run these steps after ESP-IDF 5.4.2 is installed and activated on Windows.

## 1. Build And Flash

```powershell
idf.py set-target esp32p4
idf.py build
idf.py flash monitor
```

Expected serial output:

- Pattern catalog loads with at least 24 patterns.
- Display initialization succeeds.
- Audio codec initialization succeeds.
- The `tab5_audio` task starts without repeated write failures.

## 2. Display And Touch

- The first screen shows `Aby55`.
- Tap `START`; the button changes to `STOP`.
- Tap `<` and `>`; the pattern number and name change.
- Drag BPM, Swing, and Volume sliders; labels update immediately.
- Tap `TAP` four times at a steady tempo; BPM moves toward the tapped speed.

## 3. Audio

- With no external cable, built-in speaker output is audible.
- With headphones connected to the 3.5 mm jack, headphone output is audible.
- Volume slider affects output level.
- Kick, snare/clap, hats, and percussion are distinguishable across patterns.

## 4. Timing Smoke Test

- Set BPM to 120 and play `House Classic`.
- Let it run for 10 minutes.
- Expected: no obvious UI freeze, repeated underrun log spam, silence, or runaway distortion.

## Notes

- Aby55 uses a local, patched `m5stack_tab5` BSP component for screen, touch, and codec bring-up.
- If BSP API names changed after `1.0.0`, update only `components/tab5_bsp_adapter/tab5_bsp_adapter.cpp`.
