# Third-Party Notices

Aby55 is built on top of several open-source hardware and software projects.
This file is a practical attribution note, not a replacement for the license
texts distributed by each upstream project.

## ESP-IDF

- Project: Espressif IoT Development Framework
- Source: https://github.com/espressif/esp-idf
- License: Apache License 2.0

## M5Stack Tab5 BSP

- Project: Espressif BSP for M5Stack Tab5
- Source: https://github.com/espressif/esp-bsp/tree/master/bsp/m5stack_tab5
- Component Manager package: `espressif/m5stack_tab5`
- License: Apache License 2.0
- Note: Aby55 vendors a local patched copy under `components/m5stack_tab5`
  to preserve the validated ST7121 display path and Tab5 IO expander defaults.

## LVGL

- Project: LVGL
- Source: https://github.com/lvgl/lvgl
- License: MIT License

## Espressif Component Manager Dependencies

The remaining ESP-IDF components are resolved through `dependencies.lock`.
Important packages include `esp_codec_dev`, `esp_lvgl_port`, `bmi270`,
`esp_io_expander`, `esp_io_expander_pi4ioe5v6408`, `esp_lcd_touch_st7123`,
`esp_lcd_touch_gt911`, and `usb`.

## Instrument References

Aby55 mentions drum machines, acid bass, desktop synthesizers, and hardware
sequencers as creative references. These are cultural and workflow references
only; Aby55 is not affiliated with any commercial instrument brand.
