# Firmware

ESP-IDF firmware for **ESP-Mosaico / ESP32-S31**.  
See the repository root [`README.md`](../README.md) for the project overview, interaction rules, and prebuilt firmware.

## Build

Requirements: ESP-IDF **6.1.1**, target `esp32s31`, network for the first component fetch.

```bash
. $IDF_PATH/export.sh
cd firmware
idf.py build
idf.py -p /dev/cu.usbmodem101 flash monitor
```

Third-party dependencies are declared in [`main/idf_component.yml`](main/idf_component.yml):

- `lvgl/lvgl` 9.6.0~1
- `espressif/esp_lcd_co5300` 2.2.0
- `espressif/esp_lcd_touch` 1.2.1
- `espressif/esp_lcd_touch_cst9220` 0.1.2

`managed_components/` and `build/` are generated and ignored by git. `dependencies.lock` is committed for reproducibility.

## Host tests

The `pet_core` state machine is plain C and can be tested without hardware:

```bash
./tests/run_host_tests.sh
```

## Components

| Component | Responsibility |
|---|---|
| `pet_core` | pure-C state machine: stats, sleep, feed, hug, game, growth |
| `pet_store` | NVS persistence |
| `interaction_module` | pad sampling queue, LDR, WS2812 |
| `peripherals` | LCD, touch, power, I2C |
| `pet_ui` | LVGL UI, font subset, ghost art |

## Tools

- `tools/gen_font.py` — regenerate `components/pet_ui/src/lv_font_starpet_18.c` from a TTF/OTF supplied via `PET_FONT` (the checked-in font is an OFL Noto Sans SC subset).
- `tools/gen_ghost.py` — regenerate `components/pet_ui/src/ghost_art.c` (needs Pillow).

Both generated files are committed, so the tools are optional for normal builds.
