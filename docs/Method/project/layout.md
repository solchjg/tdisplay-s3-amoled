# Project bindings — layers, source layout, toolchain, bench

## Source layout mirrors FSD §2.4

| Layer | Component | Location | Depends on |
|-------|-----------|----------|------------|
| L2 | Device Logic (FSD §5) | `components/device_logic/` — pure C, no ESP-IDF headers: both transition functions, retry delay, screen content, SSID sanitising | nothing |
| L1 | LCD Display Interface (FSD §6) | `components/lcd/` — the three-line display; layout core (`lcd_layout.c`) pure | `rm67162`, `text`, `tdisplay_s3_amoled` |
| L0 | RM67162 panel driver | `shared/components/rm67162/` — board-agnostic QSPI bring-up, DMA canvas, rectangle writes, brightness; configured by `rm67162_config_t` | esp_lcd, SPI and GPIO drivers, `esp_try` |
| L0 | Text renderer | `shared/components/text/` — 16 × 32 font and `text_render`/`text_width`, pure C | nothing |
| L0 | Board support | `shared/components/tdisplay_s3_amoled/` — header-only pin and panel constants, pure | nothing |
| L0 | BME280 sensor | `shared/components/bme280/` — calibration, integer compensation and the forced-mode sequence over an abstract bus (core pure); `i2c_master` transport configured by `bme280_config_t` | esp_driver_i2c, `esp_try` |
| L0 | Error propagation | `shared/components/esp_try/` — header-only `ESP_TRY` (return the error, never abort) | esp_err |
| L1 | Serial Log Interface (FSD §7) | `components/web_log/` (line formatting core pure; emit via esp_log) | esp_log |
| L1 | WiFi network (FSD §8) | `shared/components/wifi_net/` — station, SoftAP, stored credentials in NVS, station events through a callback | esp_wifi, esp_netif, esp_event, nvs_flash, `esp_try` |
| L1 | Provisioning Portal (FSD §13) | `shared/components/portal/` (form validation and HTML escaping core pure; httpd form and captive DNS on the SoftAP `wifi_net` starts) | esp_http_server, lwIP |
| L1 | Status HTTP Server (FSD §14) | `shared/components/status_http/` (JSON and HTML rendering core pure); reports each answer through the `log` callback in its source | esp_http_server |
| L1 | OTA Receiver (FSD §15) | `shared/components/ota_rx/` (request parsing and download state core pure; download via esp_http_client into the other slot); registers its handler on the server handle the composition root passes in and reports through `ota_rx_hooks_t` | app_update, esp_http_client, esp_http_server |
| L1 | UDP Log (FSD §16) | `shared/components/udp_log/` (line-selection core pure, tag passed in) | lwIP |
| L1 | mDNS Responder (FSD §17) | `shared/components/mdns_name/` | managed `espressif/mdns` |
| L1 | BOOT Button (FSD §18) | `shared/components/button/` (hold detector core pure); threshold and release through callbacks | GPIO driver, esp_timer |
| L1 | Deep Sleep (FSD §21) | `shared/components/deep_sleep/` (period and wake-cause core pure); arms the timer and the user-key wake (`CONFIG_DEEP_SLEEP_WAKE_GPIO` 21), runs the prepare callbacks the composition root registers (`lcd_sleep`, `wifi_net_stop`), sleeps | esp_sleep, RTC GPIO, `esp_try` |
| L1 | Time (FSD §22) | `shared/components/time_sync/` (synced-at-boot core pure); time zone from `CONFIG_TIME_SYNC_TZ`, SNTP from `CONFIG_TIME_SYNC_SERVER` started with the services, each sync reported through a callback (L20) | esp_netif SNTP |
| L1 | Weather Sensor (FSD §23) | `components/weather/` (history ring and every rendering of a reading core pure); reads the BME280 at 0x76 on GPIO 43/44, keeps 144 readings in RTC memory, registers `GET /api/readings` on the server handle and gives `status_http` the page's reading section (`extra_html`) | `bme280`, `time_sync`, esp_http_server |
| L1 | Device Identity (FSD §20) | `shared/components/identity/` (name formatting core pure, prefixes passed in) | nothing |
| — | Composition root | `main/app_main.c` — event queue, 1 s tick, runs the logic, executes the returned actions against the interfaces, and wires each interface's callbacks to `web_log` | all of the above |
| host tests | | `tests/host/` — builds `device_logic` and the pure cores with the host compiler; `test_lcd_text.py` uses the `compile_harness` fixture in `conftest.py` | the pure cores only |

The `hello_logic` and `hello_log` components are the base project's; `/build`
replaces them with `device_logic` and `web_log` and moves their tests to the new
FSD IDs. A component and its `REQUIRES` are added in the phase (FSD §3) whose
requirements first need them, never earlier.

- `device_logic` exposes one transition function per region: (state, event,
  guard result) → (next state, actions). The composition root executes the
  actions; interfaces report events back through the queue.
- `lcd` and `rm67162` return `esp_err_t` for every call and never retry or abort;
  policy belongs to `device_logic` (FSD §6.5).
- The L0 components hold nothing specific to this app, so they can be reused
  as they are: `lcd` is where the app's screen (three lines, white on black, their
  positions) and the panel settings for this board are chosen.
- The L1 interfaces except `lcd` and `web_log` are reusable too (see "Reusing
  components" below): none includes `web_log` or another app component. What
  they would log reaches the composition root through a callback, and only
  `app_main.c` calls `web_log_*`.
- Panel bring-up order (`rm67162_start`, confirmed by commissioning, capability `lcd-rm67162`): GPIO 38 high → SPI2 bus in quad mode (75 MHz requested; the divider runs it at 80 MHz) → esp_lcd SPI
  panel IO (32-bit opcode, 8-bit parameters) → RST pulse → sleep out → LilyGO's
  ELVSS/OVSS trim (pages 5 and 1) → MADCTL 0x60 (landscape, USB-C on the right) →
  16 bit/pixel → brightness 0 → clear to black → display on → draw → brightness
  0xD0. No RAM offset; RGB565 pixels byte-swapped (MSB first, `rm67162_bus_colour`)
  in the draw buffer; these are the values `lcd.c` passes in `rm67162_config_t`.
  FSD FR-6.1, FR-6.3 and FR-6.4 are the requirements; operator checks decide.
- The start guard (FSD D1/D2) is the result of `lcd_start()`: bring-up, clear
  to black, and the first draw of the BOOT screen. The panel is lit (brightness non-zero) by
  D1's action, so a failed start never lights a half-drawn panel.
- Each draw waits for the DMA-done callback (100 ms bound) before the line
  buffer is refilled; a missed callback is `ESP_ERR_TIMEOUT`, i.e. a write
  failure for FSD D5.
- Text uses a 16 × 32 fixed-width bitmap font (ASCII 0x20 – 0x7E only):
  Terminus Font Bold 16x32 (SIL OFL 1.1, `shared/components/text/font/OFL.txt`),
  converted by `shared/tools/bdf_to_c.py` into `shared/components/text/font16x32.c`.
  Regenerate, never hand-edit. Lines 1 – 3 at
  y = 36, 104, 172, all at x = 0 (recommendation; FR-6.5 is the requirement). A
  changed line is redrawn as its full 536 px-wide row, so no characters of a
  longer previous text remain (FR-6.8).
- Draw from the `rm67162` canvas, a full-width band in internal DMA-capable RAM
  (536 × 32 × 2 B = 33.5 KiB); PSRAM stays disabled.
- ESP-IDF has no RM67162 panel driver; `shared/components/rm67162/` drives it
  through esp_lcd's SPI panel IO and is the only component that declares `esp_lcd`.

## Reusing components

The reusable components live in their own repository,
[`solchjg/esp-idf-components`](https://github.com/solchjg/esp-idf-components),
checked out here as the git submodule `shared/` and found by the build through
`EXTRA_COMPONENT_DIRS` in the top-level `CMakeLists.txt` (and in each test
build under `tests/target/`). Its README lists every component, its Kconfig
options and what else it needs, and shows how another project takes them: by
IDF Component Manager git dependency pinned to a tag, or as a submodule.

- Clone with `git clone --recurse-submodules`, or run
  `git submodule update --init` in an existing clone; the CI checkouts use
  `submodules: true`.
- The submodule is pinned to one commit. To take a newer version, change the
  component in `esp-idf-components` (its own CI runs its host tests and a
  build of every component), then `git -C shared pull` here and commit the
  new pin.
- `tests/host/` compiles the shared pure cores from `shared/components/`, so
  this project's requirements stay verified against exactly the pinned code.
- The Kconfig defaults are this app's values, so this repo sets none of them.
  The identity prefixes are read by the composition root and passed to
  `id_softap_ssid` / `id_hostname`, so the `identity` core stays free of
  ESP-IDF headers.
- `lcd`, `device_logic`, `web_log` and `weather` stay in `components/`: they are this
  app's screen, state machine and log vocabulary.

## Test builds

Each is built by CI as a test artefact under `tests/target/` and never released.
Each uses the shipped `sdkconfig.defaults` and the shipped `partitions.csv`,
copied into its project directory before the build (the defaults name the
table relative to the project); never a table of its own.

### LCD fault build

`tests/target/lcd_fault/` builds the shipped `main/` and components with
`components/lcd/` replaced by a stub (`lcd_fault`) selected by a test-only
Kconfig option. The stub replaces the whole LCD interface, so the L0
components below it are not built into this variant (only `esp_try`, which
`wifi_net` needs). The stub returns a configured `esp_err_t` at init or at the
k-th draw, and logs `lcdfi: call` on every call it receives (FSD §5.6).

### No-network build (FSD FR-5.24)

`tests/target/no_network/`: the shipped `main/` with a test-only Kconfig option
that skips starting the station attempt in T2, so the image stays in CONNECTING,
never reaches T5, and is rolled back by T13. Its embedded version differs from the
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

- Testbench `192.168.2.230`, SLOT1, `rfc2217://192.168.2.230:4001`.
- EN/BOOT GPIOs on SLOT1 are not proven wired: flash and reset over USB only.
- SLOT1 can hold another project's board; confirm `detected_chip: esp32s3` and the MAC before
  any run.
- Native USB part: never open the port with DTR/RTS asserted (see
  [`../standards/testing.md`](../standards/testing.md)).
- When the USB-Serial/JTAG port comes back (after a flash, or a deep-sleep wake that stays
  up about a second), the bench re-attaches and the chip takes a USB reset, which clears RTC
  memory; the serial ring also loses about the first second after each restart. Evidence that
  must outlive a restart goes to NVS (the sleep build's `sleeptest:` boot history).
