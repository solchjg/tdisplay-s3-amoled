# Project bindings — layers, source layout, toolchain, bench

## Source layout mirrors FSD §2.4

| Layer | Component | Location | Depends on |
|-------|-----------|----------|------------|
| L2 | Hello Display Logic (FSD §5) | `components/hello_logic/` — pure C, no ESP-IDF headers | nothing |
| L1 | LCD Display Interface (FSD §6) | `components/lcd/` — the two-line display; layout core (`lcd_layout.c`) pure | `st7789`, `text`, `tdisplay_s3`, `esp_try` |
| L0 | ST7789 panel driver | `components/st7789/` — board-agnostic i80 bring-up, DMA canvas, rectangle writes; configured by `st7789_config_t` | ESP-IDF esp_lcd, GPIO driver |
| L0 | Text renderer | `components/text/` — 16 × 32 font and `text_render`/`text_width`, pure C | nothing |
| L0 | Board support | `components/tdisplay_s3/` — header-only pin and panel constants, pure | nothing |
| L0 | Error propagation | `components/esp_try/` — header-only `ESP_TRY` (return the error, never abort) | esp_err |
| L1 | Serial Log Interface (FSD §7) | `components/hello_log/` (format core pure; emit via esp_log) | esp_log |
| — | Composition root | `main/app_main.c` — wires the timer, the logic, and both interfaces | all of the above |
| host tests | | `tests/host/` — builds `hello_logic` and the pure cores with the host compiler, via the `compile_harness` fixture in `conftest.py` | the pure cores only |

- `hello_logic` exposes a transition function: (state, event, guard result) →
  (next state, actions). The composition root executes the actions.
- `lcd` and `st7789` return `esp_err_t` for every call and never retry or abort;
  policy belongs to `hello_logic` (FSD §6.5).
- The L0 components hold nothing specific to this app, so they can be reused
  as they are: `lcd` is where the app's screen (two lines, white on black, their
  positions) and the panel settings for this board are chosen.
- Panel bring-up order (`st7789_start`, confirmed by commissioning, capability `lcd-st7789`): GPIO 15 high → i80 bus (10 MHz) → ST7789 panel IO and
  panel → reset → init → colour inversion on → swap X/Y and mirror for landscape →
  gap (0, 35) → display on → clear to black → draw (`lcd_start`) → GPIO 38 high. Use exactly
  invert_color(true), swap_xy(true), mirror(false, true), RGB element order, and
  RGB565 pixels byte-swapped (MSB first, `st7789_bus_colour`) in the draw buffer
  for the 8-bit bus; these are the values `lcd.c` passes in `st7789_config_t`.
  FSD FR-6.1, FR-6.3 and FR-6.4 are the requirements; operator checks decide.
- The INIT_DONE guard (FSD T1/T2) is the result of `lcd_start()`: bring-up,
  clear to black, and the first draw of both lines. The backlight is turned on
  by T1's action, so a failed start never lights a half-drawn panel.
- Each draw waits for the DMA-done callback (100 ms bound) before the line
  buffer is refilled; a missed callback is `ESP_ERR_TIMEOUT`, i.e. a write
  failure for FSD T4.
- Text uses a 16 × 32 fixed-width bitmap font (ASCII 0x20 – 0x7E only):
  Terminus Font Bold 16x32 (SIL OFL 1.1, `components/text/font/OFL.txt`),
  converted by `tools/bdf_to_c.py` into `components/text/font16x32.c`.
  Regenerate, never hand-edit. Line 1 at
  y = 40, line 2 at y = 96, both at x = 16 (recommendation; FR-6.5 is the
  requirement). Line 2 is redrawn by drawing its full 288 px-wide row cell, so
  stale digits are overwritten (FR-5.3).
- Draw from the `st7789` canvas, a full-width band in internal DMA-capable RAM
  (320 × 32 × 2 B = 20 KiB); PSRAM stays disabled.
- The ST7789 panel driver is in-tree in ESP-IDF (`esp_lcd_new_panel_st7789`);
  only `components/st7789/` declares `esp_lcd`.

## Fault-injection test build

`tests/target/lcd_fault/` builds the shipped `main/` and components with
`components/lcd/` replaced by a stub (`lcd_fault`) selected by a test-only
Kconfig option. The stub replaces the whole two-line interface, so the L0
components below it are not built into this variant. The stub returns a configured `esp_err_t` at init or at the k-th
draw, and logs `lcdfi: call` on every call it receives (FSD §5.5). It is built by
CI as a test artefact and never released.

## Toolchain

- ESP-IDF v6.0.2 via the container image `espressif/idf:v6.0.2` (FSD C-10.2).
- `cmake_minimum_required(VERSION 3.22)` (IDF 6 minimum).
- `sdkconfig.defaults` sets target `esp32s3`, 16 MB flash, single-app partition
  table, the USB-Serial/JTAG console, and leaves SPIRAM disabled (FSD C-10.1,
  C-10.3 – C-10.5).

## Bench

- Testbench `192.168.2.230`, SLOT4, `rfc2217://192.168.2.230:4004`.
- EN/BOOT GPIOs are not wired on SLOT4: flash and reset over USB only.
- SLOT4 is shared with `esp32-oled-hello`; confirm `detected_chip: esp32s3` before
  any run.
- Native USB part: never open the port with DTR/RTS asserted (see
  [`../standards/testing.md`](../standards/testing.md)).
