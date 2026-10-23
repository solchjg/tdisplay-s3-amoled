# Project bindings — layers, source layout, toolchain, bench

## Source layout mirrors FSD §2.4

| Layer | Component | Location | Depends on |
|-------|-----------|----------|------------|
| L2 | Device Logic (FSD §5) | `components/device_logic/` — pure C, no ESP-IDF headers: both transition functions, retry delay, screen content, SSID sanitising | nothing |
| L1 | LCD Display Interface (FSD §6) | `components/lcd/` (layout and font core pure; panel I/O via esp_lcd) | esp_lcd, GPIO driver |
| L1 | Serial Log Interface (FSD §7) | `components/web_log/` (line formatting core pure; emit via esp_log) | esp_log |
| L1 | Provisioning Portal (FSD §13) | `components/portal/` (form validation and HTML escaping core pure; SoftAP, httpd and DNS glue) | esp_wifi, esp_netif, esp_http_server, lwIP |
| L1 | Status HTTP Server (FSD §14) | `components/status_http/` (JSON and HTML rendering core pure) | esp_http_server |
| L1 | OTA Receiver (FSD §15) | `components/ota_rx/` (request parsing and download state core pure; download via esp_http_client into the other slot); registers its handler on the server handle the composition root passes in | app_update, esp_http_client, esp_http_server |
| L1 | UDP Log (FSD §16) | `components/udp_log/` (prefix-stripping core pure) | lwIP |
| L1 | mDNS Responder (FSD §17) | `components/mdns_name/` | managed `espressif/mdns` |
| L1 | BOOT Button (FSD §18) | `components/button/` (hold detector core pure) | GPIO driver |
| L1 | Device Identity (FSD §20) | `components/identity/` (name formatting core pure) | esp_mac, esp_app_desc |
| — | Composition root | `main/app_main.c` — event queue, 1 s tick, runs the logic, executes the returned actions against the interfaces | all of the above |
| host tests | | `tests/host/` — builds `device_logic` and the pure cores with the host compiler | the pure cores only |

The `hello_logic` and `hello_log` components are the base project's; `/build`
replaces them with `device_logic` and `web_log` and moves their tests to the new
FSD IDs. A component and its `REQUIRES` are added in the phase (FSD §3) whose
requirements first need them, never earlier.

- `device_logic` exposes one transition function per region: (state, event,
  guard result) → (next state, actions). The composition root executes the
  actions; interfaces report events back through the queue.
- `lcd` returns `esp_err_t` for every call and never retries or aborts; policy
  belongs to `device_logic` (FSD §6.5).
- Panel bring-up order (confirmed by commissioning, capability `lcd-st7789`): GPIO 15 high → i80 bus (10 MHz) → ST7789 panel IO and
  panel → reset → init → colour inversion on → swap X/Y and mirror for landscape →
  gap (0, 35) → display on → clear to black → draw → GPIO 38 high. Use exactly
  invert_color(true), swap_xy(true), mirror(false, true), RGB element order, and
  RGB565 pixels byte-swapped (MSB first) in the draw buffer for the 8-bit bus.
  FSD FR-6.1, FR-6.3 and FR-6.4 are the requirements; operator checks decide.
- The start guard (FSD D1/D2) is the result of `lcd_start()`: bring-up, clear
  to black, and the first draw of the BOOT screen. The backlight is turned on by
  D1's action, so a failed start never lights a half-drawn panel.
- Each draw waits for the DMA-done callback (100 ms bound) before the line
  buffer is refilled; a missed callback is `ESP_ERR_TIMEOUT`, i.e. a write
  failure for FSD D5.
- Text uses a 16 × 32 fixed-width bitmap font (ASCII 0x20 – 0x7E only):
  Terminus Font Bold 16x32 (SIL OFL 1.1, `components/lcd/font/OFL.txt`),
  converted by `tools/bdf_to_c.py` into `components/lcd/font16x32.c`.
  Regenerate, never hand-edit. Lines 1 – 3 at
  y = 13, 69, 125, all at x = 0 (recommendation; FR-6.5 is the requirement). A
  changed line is redrawn as its full 320 px-wide row, so no characters of a
  longer previous text remain (FR-6.8).
- Draw from a line-sized buffer in internal DMA-capable RAM (20 chars × 16 × 32 ×
  2 B = 20 KiB); PSRAM stays disabled.
- The ST7789 panel driver is in-tree in ESP-IDF (`esp_lcd_new_panel_st7789`);
  declare `esp_lcd` only once the code includes it.

## Test builds

Each is built by CI as a test artefact under `tests/target/` and never released.
Each uses the shipped `sdkconfig.defaults` and the shipped `partitions.csv`,
copied into its project directory before the build (the defaults name the
table relative to the project); never a table of its own.

### LCD fault build

`tests/target/lcd_fault/` builds the shipped `main/` and components with
`components/lcd/` replaced by a stub (`lcd_fault`) selected by a test-only
Kconfig option. The stub returns a configured `esp_err_t` at init or at the k-th
draw, and logs `lcdfi: call` on every call it receives (FSD §5.6).

### No-confirm build (FSD FR-5.24)

`tests/target/no_confirm/`: the shipped `main/` with a test-only Kconfig option
that skips marking the image valid in T5. Its embedded version differs from the
shipped build's so the bench can tell the two apart in the boot line.

### Hang build (FSD §19.3)

`tests/target/hang/`: the shipped `main/` with a test-only Kconfig choice —
`app` blocks the application task 20 s after startup, `http` adds a `GET /hang`
handler that never returns.

## Toolchain

- ESP-IDF v6.0.2 via the container image `espressif/idf:v6.0.2` (FSD C-10.2).
- `cmake_minimum_required(VERSION 3.22)` (IDF 6 minimum).
- `sdkconfig.defaults` sets target `esp32s3`, 16 MB flash, the custom partition
  table `partitions.csv` at the repo root (FSD C-10.6), app rollback in the
  bootloader (C-10.8), the task watchdog with the timeout of FSD §19, the
  USB-Serial/JTAG console, and leaves SPIRAM disabled (C-10.1, C-10.3, C-10.5).
- Managed components are declared in `main/idf_component.yml` with an exact
  version; commit `dependencies.lock`, never `managed_components/`. The only one
  is `espressif/mdns` (FSD §17), added in Phase 3. After adding it, remove
  `build/` or run `idf.py reconfigure` so the component solver runs.
- JSON is rendered with `snprintf` in `status_http`'s pure core, so no cJSON
  dependency is needed.
- The firmware version (FSD FR-20.3) is the IDF project version, which the
  release workflow sets from the git tag.

## Bench

- Testbench `192.168.2.230`, SLOT4, `rfc2217://192.168.2.230:4004`.
- EN/BOOT GPIOs are not wired on SLOT4: flash and reset over USB only.
- SLOT4 is shared with `esp32-oled-hello`; confirm `detected_chip: esp32s3` before
  any run.
- Native USB part: never open the port with DTR/RTS asserted (see
  [`../standards/testing.md`](../standards/testing.md)).
