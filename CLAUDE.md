# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project state

ESP-IDF v6.0.2 firmware for the LilyGO T-Display-S3 AMOLED 1.91" (ESP32-S3,
RM67162 AMOLED over QSPI), verified on the Embedded AI Harness testbench: a
weather station with a WiFi portal and web status, BLE setup,
summary email, push alerts, a 4-20 mA input, a reading history in flash and an
optional cellular uplink. The FSD (`document_status: draft`) defines it; work
follows its requirements. FSD section numbers below 13 and some test IDs come
from the `tdisplay-s3-hello` base and keep their meaning.

## Commands

```bash
# The reusable components are the git submodule shared/ (solchjg/esp-idf-components)
git submodule update --init

# Firmware build (inside the devcontainer or with IDF v6.0.2 exported).
# -DSDKCONFIG keeps the generated config in build/, so sdkconfig.defaults always wins.
idf.py -B build -DSDKCONFIG=$PWD/build/sdkconfig set-target esp32s3
idf.py -B build -DSDKCONFIG=$PWD/build/sdkconfig build
# Waveshare ESP32-S3-SIM7670G-4G build (FSD §35); add ;sdkconfig.standin for the 1.3" OLED bench stand-in
idf.py -B build-ws -DSDKCONFIG=$PWD/build-ws/sdkconfig \
       -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.ws7670g" build

# Lint + host tests (what ci.yml runs)
ruff check .
pytest tests/host -q
pytest tests/host/test_device_logic.py -k <name>       # single test

# Inspect a build against FSD constraints (skips without BUILD_OUTPUT)
BUILD_OUTPUT=build pytest tests/host/test_build.py -v -rs

# Bench tests against SLOT1 (need the harness TestbenchDriver on PYTHONPATH)
PYTHONPATH=<harness>/pytest FIRMWARE_DIR=<artefact>/coldflash pytest tests/bench -v
pytest tests/bench -m journey     # JRN-01, the release-verification journey
```

The app image must fit a 4 MB OTA slot (FSD C-10.7, checked in CI). Never commit
`build/` or a generated `sdkconfig`; do commit `sdkconfig.defaults`.

## Documentation planes — read before changing anything

`docs/00-Overview.md` defines three planes, each fact stated once, all
present-tense, no history (rationale goes in commit messages):

- **WHAT** — `docs/Functionality/TDisplay-S3-Webserver-FSD.md`: requirements
  `FR-/NFR-/C-x.y` with priority and provenance (`[user]`, `[derived]`,
  `[proposed]`). The user owns it; only the user approves a requirement or number.
- **HOW** — `docs/Method/`: `AI-Workflow.md` (the loop every change follows),
  `standards/engineering.md`, `standards/testing.md`, `project/layout.md`
  (layers, panel bring-up order, toolchain, bench).
- **OPERATE** — `docs/UserDocumentation/User-Manual.md`.

The loop: locate the FSD rule → build per the Method → the test exists and passes
(a bug fix writes the failing regression test first) → reconcile docs. If no FSD
rule covers the work, define the requirement first. The project uses the
closed-loop skills `/define` (Phase 0), `/harness` (1), `/commission` (2),
`/build` (3).

## Architecture

Layered components; dependencies point one way (lower layers never include
higher ones — use callbacks registered in `app_main` if the need seems to invert):

- `components/device_logic/` (L2) — pure C, no ESP-IDF headers. The connectivity
  states (FSD §5.3, BOOT … WIFI_OFF), the screen lines and the transition steps
  (`dl_*_step`); it decides and returns actions, it never performs I/O.
- `components/lcd/` (L1) — this app's text display, composed from the L0
  parts below. Every call returns `esp_err_t`; it never retries or aborts
  (policy belongs to the logic layer). The layout (`lcd_layout.c`) is a pure core.
  On the cellular build it drives the 3.12" SSD1322 OLED instead (`lcd_oled.c`, FSD
  FR-35.16: six rows of 6 × 10 cells, `lcd_oled_layout.c` pure) through the shared `ssd1322`.
  Until the SSD1322 arrives, the bench build `sdkconfig.standin` (FR-35.22, never released)
  draws them on the 1.3" OLED on the sensor bus at 0x3C instead (`lcd_standin.c`, joining the
  bus's I2C controller; `lcd_standin_core.c` pure).
  The cellular build's optional 4.2" e-paper (FR-35.15, FR-35.24) is `lcd_epaper.c` over the
  shared `ssd1683` on the OLED's SPI2 lines (its own CS, BUSY, RST): its pure layout and
  refresh rule in `lcd_epaper_layout.c` (lines 1 – 5 in 12 × 24, line 6 and the status block
  in 8 × 16; a full refresh first, partial ones at most every 5 s, sent by `lcd_flush` each
  tick), and the sleep screen (`lcd_final`) before the controller's deep sleep. `lcd_cell.c`
  draws a window on the panel `lcd_use` names; which one is `device_logic`'s
  `dl_display_choose` (FR-35.2), from the `epaper` and `oled` settings kept with the modem's
  in `uplink`.
- `shared/components/rm67162/` (L0) — board-agnostic RM67162 QSPI driver via `esp_lcd`'s
  SPI panel IO, configured by `rm67162_config_t`; `shared/components/tdisplay_s3_amoled/`
  (L0) holds the board's pin and panel constants; `shared/components/esp_try/` (L0) the `ESP_TRY` macro.
- `shared/components/text/` (L0) — pure C font renderer. `font16x32.c` (lines 1 – 5),
  `font12x24.c` (line 6), `font8x16.c` (the e-paper's line 6) and `font6x10.c` (the OLED) are generated by
  `shared/tools/bdf_to_c.py` — regenerate, never hand-edit.
- `components/setup_check/` (L1) — the setup check (FSD §24): the pending mark
  in NVS and the keys that end the hold (user key GPIO 21, BOOT GPIO 0); `setup_check_core.c` is the pure debouncer.
  When to hold is `device_logic`'s `dl_hold_step`.
- `components/web_log/` (L1) — the `web:` serial log lines (FSD §7); `web_log_format.c` is the pure core.
  On the cellular build every line is also noted for the e-paper's status block (FR-35.24,
  `web_log_status.c` pure), kept in RTC memory.
- `components/weather/` (L1) — the BME280 task's readings and period averages (FSD §23): kept in RTC
  memory, shown on line 4, logged (L21/L22), served at `/api/readings`;
  `weather_core.c` is the pure core. The sensor driver is `shared/components/bme280/`.
- `components/history/` (L1) — the reading history (FSD §27): each kept reading (24 bytes) appended to the
  `history` partition (2 MB) through the shared `flash_ring`, an old 16-byte history erased once (L36), served as CSV at `/api/history`
  (`?from=&to=` dates) and paged over BLE; `history_core.c` is the pure core.
- `components/loop/` (L1) — the 4-20 mA loop input (FSD §29): settings in NVS, the
  GPIO 3 supply (also the isolator's VCC1), the loop ADS1115 on its own bus (I2C port 1, GPIO 15/16; shared
  `ads1115`, scaled by shared `loop420`) read after
  each BME280 reading through `weather_set_ext`; `loop_core.c` is the pure core (settings,
  L34/L35). Its value is add-on slot 2 of §29.2: a float (NaN for none) in every reading,
  average, history record, `ext` in `/api/readings`, line 6, the summary and the §28 verdict;
  an add-on that is off shows nowhere.
- `components/sampling/` (L1) — sampling setups (FSD §28): the selection (`std`, `fine`, `slow`,
  `custom`, plus the `quick`, `cycle` and `gps` tests, `gps` the FR-33.20 search) from the portal form or a BLE write, kept in NVS;
  the run of tests in RTC memory; `sampling_core.c` is the pure core (selection, test progress,
  verdict, L32/L33). The double press of the user key is `setup_check_core`'s `sk_taps_sample`.
- `components/summary/` (L1) — the summary email (FSD §25): mail settings from
  the portal form in NVS, the due rule, the message (`summary_core.c` pure), sent
  through the shared `mail_tx`; a timer wake starts WiFi only for a due summary.
- `components/notify/` (L1) — push alerts (FSD §31): ntfy settings from the portal form or a BLE
  write in NVS, the condition state (limits with hysteresis, failures, one retry) in RTC memory,
  titles and bodies (`notify_core.c` pure), sent through the shared `ntfy_tx`; a timer wake starts
  WiFi only for due alerts or a due summary.
- `components/uplink/` (L1) — the cellular uplink (FSD §33): the §33.3 settings from the
  portal form or a BLE write and the GPS place in NVS, and the Notecard requests (setup,
  `note.add`, `card.time`, location) over the shared `notecard` (Blues' note-c, vendored) on
  the BME280's bus; `uplink_core.c` is the pure core (settings, which path each send takes,
  the `alert.qo` and `mail.qo` bodies, the clock and GPS steps, notes of at most 100
  readings, line 6 during the search). With `uplink` `cell`, alerts and email go out as
  notes and a timer wake never starts WiFi; the ThingsBoard notes wait for §32.
- `components/board/` (L0) — the build-time board choice (`CONFIG_APP_BOARD_*`, FSD §35,
  FR-35.5) and `board.h`, the pins this app wires on each board.
- `components/modem_link/` (L1) — the cellular build's link over the Waveshare board's SIM7670G
  (FSD §35): the shared `sim7670`, its supply switched on by GPIO 21 (the board's 4G switch off),
  an attach with the network lock (LTE Cat-1 has no PSM), PPP as the default route for the
  existing senders, then `AT+CPOF` and the supply cut (FR-35.23); the network's time after
  registering and the GPS search with the modem's GNSS (LTE off while it runs). The project's
  `CMakeLists.txt` keeps it, `sim7670`, `sim7080` and `max17048` out of the T-Display build; `main`
  links it only with the Waveshare board.
- `components/settings_card/` (L1) — settings from microSD on the cellular build (FSD
  FR-35.4, FR-35.19, FR-35.20): on a boot that is not a timer wake, the always-powered slot
  mounted, `/settings.txt` read and applied as a BLE `settings` write, `/settings.done` without
  secrets and `/settings.result` written, unmounted; `settings_card_core.c` is the pure core
  (the file as the BLE body, the `.done` text).
- `components/ble_settings/` (L1, pure) — BLE setup (FSD §26): what a `settings`
  write means in each window (absent secrets keep the stored ones in a key
  window), the `settings` read without secrets, the field rules the page checks
  (`bset_form`), and `info`. The transport is the
  shared `ble_setup`; when a window opens and closes is `device_logic`'s
  `dl_ble_step`.
- `main/app_main.c` — composition root: runs the tick, calls the logic, executes
  the returned action bitmask against the interfaces.
- Reusable components (`wifi_net`, `portal`, `status_http`, `ota_rx`, `udp_log`,
  `mdns_name`, `button`, `deep_sleep`, `time_sync`, `identity`, `bme280`, `ads1115`, `loop420`, `mail_tx`, `ntfy_tx`, `notecard`, `ble_setup`, `flash_ring`, `sim7670`, `sim7080`, `max17048`, `axp2101`, `ssd1322`, `ssd1683` and the L0 parts) live in the separate repo
  `solchjg/esp-idf-components`, checked out as the submodule `shared/` and added
  via `EXTRA_COMPONENT_DIRS`; change them there, then bump the pin here. They
  never include `web_log` or another app component: they report through
  callbacks that `app_main.c` wires to `web_log`, and take app-specific values
  from their own Kconfig options. See "Reusing components" in `docs/Method/project/layout.md`.

Pure cores exist so `tests/host/` can compile them with the host `cc` and drive
them through a small C harness; keep new logic ESP-IDF-free for the same reason.
Never use `ESP_ERROR_CHECK` on display errors — report them to the state machine.
Only list component `REQUIRES`/`PRIV_REQUIRES` that are actually included today.

Panel bring-up order and the exact RM67162 settings (GPIO 38 power, QSPI 75 MHz requested (80 MHz actual),
MADCTL 0x60, no offset, byte-swapped RGB565, brightness 0 until a successful start)
are in `docs/Method/project/layout.md`; they were confirmed by commissioning.

Other builds sharing the components:
- `tests/target/lcd_fault/` — shipped `main/` with `lcd` replaced by a
  fault-injection stub (variants `sdkconfig.init`, `sdkconfig.write5`); test
  artefact only, never released.
- `tests/target/sleep/` — shipped `main/` with a stand-in task that sleeps
  `SLEEP_S` (10 or 300) seconds twice per power-on (FSD §21.3); test artefact only.
- `tests/target/mail_fast/` — shipped `main/` with one `mail_every` unit worth 5
  minutes instead of an hour (FSD §25); test artefact only.
- `tests/target/hang/` — shipped `main/` with a test-only hang (`app` task or `http`
  handler) for the watchdog tests; `tests/target/no_network/` — shipped `main/` that
  never starts a station attempt (FR-5.24 rollback); test artefacts only.
- `commissioning/{forward-path,lcd-probe,wifi-probe,button-probe}/` — Phase 2 bring-up programs;
  `commissioning/notecard-probe/` — the Notecard bring-up (FSD §33, Phase 15: bus, version,
  transport, note timing, optional sync and GPS);
  `commissioning/sat-probe/` — the satellite trial (no FSD section yet): the pure `sat_day`
  packer of the compact `day.qo` note (24 hourly loop averages, 57 bytes), the Notehub route's
  `route.jsonata`, `tools/sat_ref.py` (reference decoder and sample-data generator, which also
  writes `main/sample_days.h`) and the program that sends those days through the Notecard over
  `cell` or `ntn` (Skylo), held together by `tests/host/test_sat_day.py`;
  `commissioning/loop-probe/` — the 4-20 mA loop hardware check (FSD §29, run before its
  bench tests); `commissioning/ws-probe/` — the Waveshare SIM7670G board's hardware check (FSD §35, Phase 17:
  chip and MAC, the sensor bus and fuel gauge, microSD and card detect, the modem over UART 1 with its GPIO 21
  supply, GNSS, the off-board key, the pin walk and the sleep current, `docs/Method/project/ws-probe.md`);
  `commissioning/oled-probe/` — the 1.3" OLED stand-in alone on the Waveshare board (FR-35.22): every 3 s an
  I2C scan of GPIO 15/16 in both pin orders and a test pattern on a panel at 0x3C/0x3D, the modem kept off;
  `commissioning/ws-epaper-probe/` — the Waveshare 4.2" e-Paper Module (V2, SSD1683) on the Waveshare board (§36):
  a full refresh, five timed partial refreshes and the controller's deep sleep every 30 s, the modem kept off;
  `commissioning/ws-lcd-probe/` — the Waveshare 1.9" LCD module (170 × 320, ST7789) on the Waveshare board through the
  shared `st7789`'s SPI mode, on SPI2 beside the e-paper and OLED with its own CS on GPIO 19 (no FSD section: the panel
  is for another project): timed fills, a frame with corner marks, colour bars, a moving box's frame rate, the modem kept off;
  `commissioning/modem-probe/` — the LTE-M modem evaluation on a LilyGO
  T-SIM7080G-S3 (no FSD section yet, `docs/Method/project/lte-m-modem.md`); `commissioning/epaper-probe/` —
  the e-paper bring-up on an Elecrow CrowPanel ESP32 E-Paper 4.2" (SSD1683 panel, Qwiic bus,
  sleep current; no FSD section yet, `docs/Method/project/crowpanel-epaper.md`); `commissioning/common/oled/`
  is the modem probe's SSD1306 status screen. The LoRaWAN site survey for the Heltec
  lives in `solchjg/lora`. Never released.

## Testing

- Tiers: **host** (pure logic, every commit), **target** (DUT in SLOT1), **bench**
  (DUT + operator prompts). Every test is declared in `testing/test-plan.yaml`
  with the requirement IDs it verifies; status values (`not done`/`successful`/
  `failed`) are written by the runner, never by hand.
- An unmet precondition (no bench, wrong DUT, no `FIRMWARE_DIR`) is
  `pytest.skip` → `not done`, never a failure. A not-yet-written module is `xfail`.
- Gates: `testing/gates/` — a fresh checker reads the gate file + repo and
  returns OPEN/SHUT. `testing/debugging-agenda.md` tracks commissioning doubts.

## Bench / hardware

- Testbench `http://192.168.2.230:8080`, slot **SLOT1**, serial
  `rfc2217://192.168.2.230:4001`. Confirm `detected_chip: esp32s3` and the
  recorded MAC `48:27:e2:ea:48:3c` before any run: the slot can hold another
  project's board.
- EN/BOOT GPIOs on SLOT1 are not proven wired: flash and reset over USB only.
- Native USB-Serial/JTAG: never open the port with DTR/RTS asserted (halts the
  chip) — use the testbench's serial monitor. Use unanchored monitor patterns.
- Flash via `/api/flash` with the build's `flash_args` plus the images it names
  (see `flash_coldflash` in `tests/bench/conftest.py`). Leave the rig with the
  shipped build flashed and the DUT ONLINE on the bench AP (`tds3-bench`); the
  bench pytest session's teardown starts the AP for that.
- The bench runs `solchjg/Embedded-AI-Harness` `main`, which also provides the
  pytest `TestbenchDriver`. After a harness merge, on the Pi:
  `sudo bash pi/update-bench.sh --pin SLOT1=48:27:E2:EA:48:3C`.

## CI

- `ci.yml` — ruff + host tests; builds the devcontainer and checks IDF v6.0.2.
- `build.yml` — the firmware and the test builds (lcd-fault, no-network, hang, sleep,
  mail-fast) on push and on pull requests; on `v*.*.*` tags, flashes the
  built bytes and runs the journey on the self-hosted `testbench` runner (from
  `.devcontainer/`), and releases only if that passes.
- `commissioning.yml` — builds the commissioning programs.
- Keep `IDF_TAG` and the `espressif/idf:v6.0.2` container tag in step.
