# Project bindings — layers, source layout, toolchain, bench

## Source layout mirrors FSD §2.4

| Layer | Component | Location | Depends on |
|-------|-----------|----------|------------|
| L2 | Hello Display Logic (FSD §5) | `components/hello_logic/` — pure C, no ESP-IDF headers | nothing |
| L1 | LCD Display Interface (FSD §6) | `components/lcd/` (layout and font core pure; panel I/O via esp_lcd) | ESP-IDF esp_lcd, GPIO driver |
| L1 | Serial Log Interface (FSD §7) | `components/hello_log/` (format core pure; emit via esp_log) | esp_log |
| — | Composition root | `main/app_main.c` — wires the timer, the logic, and both interfaces | all of the above |
| host tests | | `tests/host/` — builds `hello_logic` and the pure cores with the host compiler | the pure cores only |

- `hello_logic` exposes a transition function: (state, event, guard result) →
  (next state, actions). The composition root executes the actions.
- `lcd` returns `esp_err_t` for every call and never retries or aborts; policy
  belongs to `hello_logic` (FSD §6.5).
- Panel bring-up order (confirmed by commissioning, capability `lcd-st7789`): GPIO 15 high → i80 bus (10 MHz) → ST7789 panel IO and
  panel → reset → init → colour inversion on → swap X/Y and mirror for landscape →
  gap (0, 35) → display on → clear to black → draw → GPIO 38 high. Use exactly
  invert_color(true), swap_xy(true), mirror(false, true), RGB element order, and
  RGB565 pixels byte-swapped (MSB first) in the draw buffer for the 8-bit bus.
  FSD FR-6.1, FR-6.3 and FR-6.4 are the requirements; operator checks decide.
- The INIT_DONE guard (FSD T1/T2) is the result of `lcd_start()`: bring-up,
  clear to black, and the first draw of both lines. The backlight is turned on
  by T1's action, so a failed start never lights a half-drawn panel.
- Each draw waits for the DMA-done callback (100 ms bound) before the line
  buffer is refilled; a missed callback is `ESP_ERR_TIMEOUT`, i.e. a write
  failure for FSD T4.
- Text uses a 16 × 32 fixed-width bitmap font (ASCII 0x20 – 0x7E only):
  Terminus Font Bold 16x32 (SIL OFL 1.1, `components/lcd/font/OFL.txt`),
  converted by `tools/bdf_to_c.py` into `components/lcd/font16x32.c`.
  Regenerate, never hand-edit. Line 1 at
  y = 40, line 2 at y = 96, both at x = 16 (recommendation; FR-6.5 is the
  requirement). Line 2 is redrawn by drawing its full 288 px-wide row cell, so
  stale digits are overwritten (FR-5.3).
- Draw from a line-sized buffer in internal DMA-capable RAM (16 chars × 16 × 32 ×
  2 B = 16 KiB); PSRAM stays disabled.
- The ST7789 panel driver is in-tree in ESP-IDF (`esp_lcd_new_panel_st7789`);
  declare `esp_lcd` only once the code includes it.

## Fault-injection test build

`tests/target/lcd_fault/` builds the shipped `main/` and components with
`components/lcd/` replaced by a stub (`lcd_fault`) selected by a test-only
Kconfig option. The stub returns a configured `esp_err_t` at init or at the k-th
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
