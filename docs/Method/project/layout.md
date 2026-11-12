# Project bindings — layers, source layout, toolchain, bench

## Source layout mirrors FSD §2.4

| Layer | Component | Location | Depends on |
|-------|-----------|----------|------------|
| L2 | Device Logic (FSD §5) | `components/device_logic/` — pure C, no ESP-IDF headers: both transition functions, retry delay, screen content, SSID sanitising | nothing |
| L1 | LCD Display Interface (FSD §6) | `components/lcd/` — the six-line text display; layout core (`lcd_layout.c`) pure. On the cellular build `lcd_oled.c` draws the lines on the SSD1322 OLED (FR-35.16: 6 × 10 cells, 42 characters from x 2, rows from y 2, `lcd_oled_layout.c` pure), display off in `lcd_sleep`, the off-board key's LED as FR-24.9's; with `sdkconfig.standin` (FR-35.22, bench only) `lcd_standin.c` draws them on the 1.3" OLED on the sensor bus at 0x3C (I2C controller 0, joined or opened), 21 characters a line (`lcd_standin_core.c` pure) | `rm67162`, `ssd1322`, `text`, `tdisplay_s3_amoled`, `board`, `esp_driver_i2c` |
| L0 | RM67162 panel driver | `shared/components/rm67162/` — board-agnostic QSPI bring-up, DMA canvas, rectangle writes, brightness; configured by `rm67162_config_t` | esp_lcd, SPI and GPIO drivers, `esp_try` |
| L0 | Text renderer | `shared/components/text/` — 16 × 32 and 12 × 24 fonts, `text_render`/`text_width` and the font-taking `text_font_render`, pure C | nothing |
| L0 | Board support | `shared/components/tdisplay_s3_amoled/` — header-only pin and panel constants, pure | nothing |
| L0 | BME280 sensor | `shared/components/bme280/` — calibration, integer compensation and the forced-mode sequence over an abstract bus (core pure); `i2c_master` transport configured by `bme280_config_t` | esp_driver_i2c, `esp_try` |
| L0 | Real-time clock | `shared/components/rv8803/` — the RV-8803-C7's time registers, flags and rate trim (core pure); `i2c_master` transport configured by `rv8803_config_t`, each call joining the bus open on the port or opening one for itself | esp_driver_i2c, `esp_try` |
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
| L1 | Hardware clock (FSD §39) | `components/rtc_clock/` (boot choice, phone and Notecard acceptance, drift and trim core pure); reads the optional RV-8803 at 0x32 on the sensor bus at boot to set the clock, writes it after each network sync and phone time, keeps the last network set time in NVS for the drift (L55), from the application task only | `rv8803`, `time_sync`, `board`, nvs_flash |
| L1 | Weather Sensor (FSD §23) | `components/weather/` (history ring, period averages and every rendering of a reading core pure); reads the BME280 at 0x76, else 0x77, on GPIO 43/44, keeps 144 readings, 96 averages and the summary's min/max in RTC memory, each with the two add-on values of §29.2 (float, NaN for none) filled by the callback set with `weather_set_ext`, registers `GET /api/readings` on the server handle and gives `status_http` the page's reading section (`extra_html`) | `bme280`, `time_sync`, esp_http_server |
| L1 | Reading History (FSD §27) | `components/history/` (record, CSV line, date range and BLE pager core pure); appends each kept reading (a 24-byte record) to the `history` partition (C-10.10, 2 MB) through `flash_ring`, erases a history of the old 16-byte records once (the format kept in NVS namespace `history`), its end kept in RTC memory, registers `GET /api/history` when the partition exists, pages the same lines for `ble_setup`'s `history` characteristic, erases on a factory reset | `flash_ring`, `weather`, nvs_flash, esp_http_server |
| L1 | 4-20 mA Loop Input (FSD §29) | `components/loop/` (settings, add-on slot and L34/L35 core pure); keeps the loop settings in NVS (namespace `loop`), adds its section to the portal form, drives the isolated supply's enable on GPIO 3, which also powers the isolator's board side, and reads the loop ADS1115 (0x49, A0 – A1, ±4.096 V, 128 SPS, mean of 4) on its own bus (I2C port 1, GPIO 15/16), opened for the read and left floating otherwise; the composition root registers the read with `weather_set_ext`, so it runs after each BME280 reading | `ads1115`, `loop420`, `weather`, `portal`, nvs_flash, GPIO driver |
| L1 | Sampling Setups (FSD §28) | `components/sampling/` (selection, test progress, verdict and log-text core pure); keeps the chosen setup and the tests still to run in NVS, the run of tests in RTC memory, and adds its section to the portal form | `summary` (the collect core), `portal`, nvs_flash |
| L1 | Setup Check (FSD §24) | `components/setup_check/` (key debounce core pure); the pending mark in NVS (`CONFIG_SETUP_CHECK_NVS_NAMESPACE`) and the keys that end the hold (`CONFIG_SETUP_CHECK_KEY_GPIO` 21, the user key; `CONFIG_SETUP_CHECK_KEY2_GPIO` 0, BOOT) sampled every 10 ms, each press through a callback; when to hold is `device_logic`'s | nvs_flash, GPIO driver, esp_timer |
| L1 | Summary Email (FSD §25) | `components/summary/` (settings check, due rule and message core pure); adds the mail and reading-time fields to the portal (`portal_start_extra`), keeps the settings, the sample and average minutes and the last due slot in NVS (namespace `mail`), sends through `mail_tx` in a helper task while feeding the caller's watchdog; on a timer wake the composition root brings the station up only for a due summary | `mail_tx`, `portal`, `weather`, nvs_flash, esp_timer |
| L1 | Push Alerts (FSD §31) | `components/notify/` (settings check, condition state with hysteresis, titles and bodies core pure); adds the alert fields to the portal form, keeps the settings in NVS (namespace `ntfy`) and the condition state in RTC memory, sends through `ntfy_tx` in a helper task while feeding the caller's watchdog; the composition root observes each reading and, on a timer wake, starts the station once for due alerts and a due summary | `ntfy_tx`, `weather`, `portal`, nvs_flash, esp_timer |
| L1 | Cellular Uplink (FSD §33) | `components/uplink/` (settings, send path, `alert.qo`/`mail.qo` bodies, clock and GPS steps core pure); keeps the settings and the GPS place in NVS, adds its section to the portal form, and sends the Notecard requests through `notecard` on the BME280's bus | `notecard`, `portal`, nvs_flash |
| L1 | Modem Link (FSD §35) | `components/modem_link/`, in the cellular build only (the project's `CMakeLists.txt` reads the board choice and excludes it, `sim7670`, `sim7080` and `max17048` from the T-Display build; `main` links it once `CONFIG_APP_BOARD_WS_SIM7670G` is known): the SIM7670G's supply switched on by GPIO 21 (the 4G switch off) and its first answer, the SIM awaited, an attach with the network lock, PPP as the default route for §25's and §31's senders, the network's time read after registering (FR-35.13, UTC), then `AT+CPOF` and the supply cut and held low (FR-35.23); the GPS search with the modem's GNSS (LTE off with `AT+CFUN=0` while it runs, paused by a link, FR-35.14); the settings, wake decision, link verdict, clock source and GNSS answers are `uplink`'s pure core. Its waits can pass the watch's 10 s, so the composition root pauses the watch for each modem call (FR-35.21; FR-35.9 bounds them) | `sim7670`, `board`, `uplink`, esp_netif |
| L1 | Settings card (FSD §35, FR-35.4, FR-35.19, FR-35.20) | `components/settings_card/`: the core (`settings_card_core.c`, pure: the file as the BLE body, the `.done` text) in both builds, the card (`settings_card.c`: the always-powered slot, SDMMC 1-bit on the pins of `board.h`, FAT with long names) in the cellular build only | `fatfs`, `esp_driver_sdmmc`, `sdmmc`, `vfs`, `board` |
| L0 | Board choice (FSD §35) | `components/board/` — the Kconfig board choice and `board.h`, the pins this app wires on each board | nothing |
| L1 | BLE Settings (FSD §26) | `components/ble_settings/` (pure): what a `settings` write means in each window, the read without secrets, the page's field rules, `info`; the transport is `shared/components/ble_setup/` | nothing |
| L1 | Device Identity (FSD §20) | `shared/components/identity/` (name formatting core pure, prefixes passed in) | nothing |
| — | Composition root | `main/app_main.c` — event queue, 1 s tick, runs the logic, executes the returned actions against the interfaces, and wires each interface's callbacks to `web_log` | all of the above |
| host tests | | `tests/host/` — builds `device_logic` and the pure cores with the host compiler; `test_lcd_text.py` uses the `compile_harness` fixture in `conftest.py` | the pure cores only |

A component and its `REQUIRES` are added in the phase (FSD §3) whose
requirements first need them, never earlier.

- `device_logic` exposes one transition function per region: (state, event,
  guard result) → (next state, actions). The composition root executes the
  actions; interfaces report events back through the queue.
- `lcd` and `rm67162` return `esp_err_t` for every call and never retry or abort;
  policy belongs to `device_logic` (FSD §6.5).
- The L0 components hold nothing specific to this app, so they can be reused
  as they are: `lcd` is where the app's screen (six lines, white on black, their
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
- Text uses fixed-width bitmap fonts (ASCII 0x20 – 0x7E only): Terminus Font
  Bold 16x32 on lines 1 – 5 and 12x24 on line 6 (SIL OFL 1.1,
  `shared/components/text/font/OFL.txt`), converted by `shared/tools/bdf_to_c.py`
  into `shared/components/text/font16x32.c` and `font12x24.c`
  (`bdf_to_c.py ter-u24b.bdf 12x24`). The cellular build's OLED uses the
  X11 misc-fixed 6x10 font (public domain, `bdf_to_c.py 6x10.bdf 6x10` into
  `font6x10.c`). Regenerate, never hand-edit. Line tops
  are 8, 48, 88, 128, 168 and 208 px (`lcd_layout.c`). Lines 1 – 3 at
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
- `lcd`, `device_logic`, `web_log`, `weather`, `history`, `setup_check`,
  `summary`, `sampling`, `loop`, `notify`, `uplink` and `ble_settings` stay in
  `components/`: they are this app's screen, state machine, log vocabulary,
  sensor, reading history, setup check, summary email, sampling setups, loop
  input, push alerts, cellular uplink and the meaning of its BLE settings body.

## BLE setup (FSD §26)

- `shared/components/ble_setup` is the transport: NimBLE peripheral, the GATT
  service, passkey pairing, started and stopped by the composition root. It
  never stores anything; it hands the staged body to `on_apply`.
- `components/ble_settings` (pure) merges a key-window body with the stored
  secrets and validates it with the portal's and the summary's cores, and
  renders the `settings` read and the `info` value.
- `device_logic`'s BLE region (`dl_ble_step`) decides when a window opens and
  closes; the user-key 3 s hold comes from `setup_check_hold_start`.
- NimBLE needs `CONFIG_BT_ENABLED` and `CONFIG_BT_NIMBLE_ENABLED` (one
  connection, bonds in NVS (`BLE_SETUP_BOND`, `BT_NIMBLE_NVS_PERSIST`, at most 3), no observer role; the central role only because the
  GATT client needs it: a phone indicates Service Changed on connecting, and
  an unconfirmed indication makes it drop the link after 30 s) in `sdkconfig.defaults`; it adds about
  360 KB to the image. With Bluetooth enabled ESP-IDF requires WiFi modem
  sleep, which `wifi_net` already leaves on.

## Deep sleep (FSD §21)

Reference: done.land, [Adding Deep Sleep](https://done.land/components/microcontroller/families/esp/esp32/developmentboards/esp32s/t-display/programming/usingesphome/addingdeepsleep/)
(ESPHome, on the original ESP32 T-Display with an ST7789 panel, not this board).
What carries over, and how this firmware applies it:

- The panel controller draws milliamps unless told to sleep (about 6.5 mA there):
  `rm67162_sleep` sends brightness 0, DISPOFF and SLPIN, waits 120 ms, then cuts
  the panel supply (GPIO 38) and holds it low through the sleep.
- An ext0 / RTC-IO wake costs about 2 mA more there than ext1: the user key is
  armed with `esp_sleep_enable_ext1_wakeup_io` (any low).
- A key that is still held when the device sleeps wakes it at once, so the key
  that wakes the device never puts it to sleep; GPIO 0 (BOOT) stays off the wake
  mask because it is a strapping pin.
- Its whole-board figure (about 300 µA, with the battery-ADC supply off) is for
  that board; this one draws 451 µA asleep from a 4.07 V battery with the sensor bus fitted and 380 µA without it, measured by hand (`sleep-current.md`, R-17).

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

### Fast mail build (FSD §25)

`tests/target/mail_fast/`: the shipped `main/` with `SM_EVERY_UNIT_S` set to 300,
so one `mail_every` unit is 5 minutes instead of an hour. It keeps the stored
settings, so a flash without an erase needs no new provisioning.

## Wiring

Fritzing sketches of every wired subsystem (board and BME280, the 4-20 mA loop, the
water probe, RS232, the Notecard), with their pictures and the checks to make
before wiring, are in [`wiring/wiring.md`](wiring/wiring.md), with LilyGO's pinout picture of
the board ([`wiring/lilygo-t-display-s3-amoled-pinout.jpg`](wiring/lilygo-t-display-s3-amoled-pinout.jpg)).
The Notecard and Notecarrier chosen for the cellular uplink (FSD §33), with their
power and data figures, are in [`blues-cellular.md`](blues-cellular.md).
The cellular build's two displays on the Waveshare board (the SSD1322 OLED and the
4.2" e-paper on one SPI bus), the OLED module's interface links and header and the
e-paper's reset timing are in [`ws-displays.md`](ws-displays.md).

## Toolchain

- ESP-IDF v6.0.2 via the container image `espressif/idf:v6.0.2` (FSD C-10.2).
- `cmake_minimum_required(VERSION 3.22)` (IDF 6 minimum).
- `sdkconfig.defaults` sets target `esp32s3`, 16 MB flash, the custom partition
  table `partitions.csv` at the repo root (FSD C-10.6), app rollback in the
  bootloader (C-10.8), the task watchdog with the timeout of FSD §19, the
  USB-Serial/JTAG console, and leaves SPIRAM disabled (C-10.1, C-10.3, C-10.5).
- Managed components are declared in the `idf_component.yml` of the component
  that uses them, with an exact version; commit `dependencies.lock`, never
  `managed_components/`. The only one is `espressif/mdns` (FSD §17), declared by
  `shared/components/mdns_name/`. After adding it, remove
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
- The bench runs `solchjg/Embedded-AI-Harness` `main`. After a harness merge, or after
  changing a hub or cable, update it on the Pi from its clone with
  `sudo bash pi/update-bench.sh --pin SLOT1=48:27:E2:EA:48:3C`; the pytest `TestbenchDriver`
  comes from the same branch.
- A bench pytest session leaves a fresh bench AP running, so a DUT provisioned for it ends
  ONLINE. `reset_and_follow` resets once more when its capture holds no `rst:` line.
- When the USB-Serial/JTAG port comes back (after a flash, or a deep-sleep wake that stays
  up about a second), the bench re-attaches and the chip takes a USB reset, which clears RTC
  memory; the serial ring also loses about the first second after each restart. Evidence that
  must outlive a restart goes to NVS (the sleep build's `sleeptest:` boot history).
- A wake that sends a summary stays up long enough for that re-attach, so on the bench
  USB each summary covers only its boot's readings and has no table. A test of summaries
  across wakes runs with the board off the bench USB, its emails as the evidence.
- Outdoors, the DUT runs on a second testbench next to it, the outdoor node, with its
  acceptance tests ODN-01 … ODN-13: [`outdoor-node.md`](outdoor-node.md).
- The DUT's station MAC is in eFuse and survives flashes and factory resets. The router
  reserves `192.168.40.150` for it on the `IOTID` network.
