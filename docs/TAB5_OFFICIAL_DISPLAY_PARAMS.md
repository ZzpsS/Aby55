# Tab5 Official Display Parameters

This note records the display/touch parameters extracted from the official
M5Tab5 UserDemo so we can stop relying on trial-and-error panel tests.

## Sources

- Official demo archive: M5Stack Tab5 UserDemo zip downloaded from the official documentation.
- Extracted source: a local, untracked copy of the official demo.
- Display BSP source:
  `platforms\tab5\components\m5stack_tab5\m5stack_tab5.c`
- ST7121 LCD driver source:
  `platforms\tab5\components\esp_lcd_st7121`

## Board Detection

Official detection order:

1. Probe GT911 touch.
   - If present, display is ILI9881C + GT911.
2. Probe touch controller at I2C address `0x55`.
   - Create ST7123 touch IO.
   - Read register `0x0000` as `fw_version`.
   - `fw_version == 1`: use ST7121 display path.
   - `fw_version == 3`: use ST7123 display path.
   - Otherwise fallback to ST7123 display path.

Observed on our Tab5:

- Touch firmware reported `Firmware version: 1(1.80.1.16)`.
- Therefore this device should use the official ST7121 display path, while
  still using the ST7123-style touch controller at address `0x55`.

## MIPI DSI / DPI Parameters

Parameters used by the official ST7121/ST7123 display path:

| Parameter | Value |
| --- | --- |
| DSI lane bit rate | `965 Mbps` |
| Horizontal resolution | `720` |
| Vertical resolution | `1280` |
| DPI clock | `70 MHz` |
| DPI input color format | `LCD_COLOR_FMT_RGB565` |
| Panel data endian | `LCD_RGB_DATA_ENDIAN_LITTLE` |
| Panel bits per pixel | `24` |
| RGB element order | `LCD_RGB_ELEMENT_ORDER_RGB` |
| Frame buffers | `1` |
| DMA2D | enabled |
| Reset GPIO | `-1` |

## Timing

Common horizontal timing for ST7121/ST7123:

| Parameter | Value |
| --- | --- |
| HSYNC pulse width | `2` |
| HSYNC back porch | `40` |
| HSYNC front porch | `40` |

ST7121 vertical timing:

| Parameter | Value |
| --- | --- |
| VSYNC pulse width | `20` |
| VSYNC back porch | `24` |
| VSYNC front porch | `200` |

ST7123 vertical timing:

| Parameter | Value |
| --- | --- |
| VSYNC pulse width | `2` |
| VSYNC back porch | `8` |
| VSYNC front porch | `220` |

## Driver Choice

For touch firmware `1`, use:

- `esp_lcd_new_panel_st7121`
- `st7121_vendor_config_t`
- `init_cmds = NULL`
- `init_cmds_size = 0`

For touch firmware `3`, use:

- `esp_lcd_new_panel_st7123`
- `st7123_vendor_config_t`
- official ST7123 vendor-specific init sequence

## Current Hypothesis Under Test

The black-screen/flash-then-black behavior is caused by initializing a
touch-firmware-1 Tab5 as ST7123 instead of ST7121. The current test firmware
only changes the panel detection and display parameters to match the official
ST7121 path.

## Follow-Up Finding

The ST7121 panel parameters alone were not enough. The first failing test
showed both touch probes returned `ESP_ERR_NOT_FOUND`, so the app fell back to
the ILI9881C path and later crashed during GT911 touch init.

The missing piece was the official IO expander default state. The official demo
initializes PI4IOE1 at `0x43` with:

- IO direction `0b01111111`
- high-Z register `0b00000000`
- pull select `0b01111111`
- pull enable `0b01111111`
- output register `0b01110110`

That output value pulls P1, P2, P4, P5, and P6 high. P5 is the touch reset /
enable path used by the touch controller. After applying this default state in
the managed BSP, the board probe changed to:

```text
Board probe ST712x 0x55: ESP_OK
Discovered board version 3 (LCD ST7121, Touch ST712x FW 1)
ST7123: Firmware version: 1(1.80.1.16), Max.X: 720, Max.Y: 1280, Max.Touchs: 10
tab5_drummer: app ready
```

The practical baseline is therefore:

1. Apply the official PI4IOE default output/direction/high-Z/pull state.
2. Probe touch at `0x55`.
3. If FW is `1`, use the ST7121 panel driver and timing above.
4. Keep these validated changes in the local `components/m5stack_tab5` BSP component.

## UI Stability Baseline

After display and touch were restored, the next failure was a UI freeze during
page switching. The decoded stack stopped in LVGL inside `lv_obj_set_size()`,
called from `UiController::button()`, while the app was rebuilding page
controls.

The stable UI path is:

1. Keep the official ST7121 display/touch baseline above.
2. Use ST712x touch in polling mode (`int_gpio_num = GPIO_NUM_NC`) on this
   device.
3. Use plain `lv_obj_create()` plus a centered label for buttons instead of
   `lv_button_create()`.
4. Do not create labels for empty step-grid cells.
5. When rebuilding the page body, temporarily disable LVGL display
   invalidation, clean/recreate only `page_root`, then re-enable invalidation
   and invalidate `page_root` once.

This keeps the current four-page UI clickable without returning to display
parameter experiments.
