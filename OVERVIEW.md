# Project overview

A plain-language snapshot of the whole project: what the device is, what works
on `main`, what is in flight, and how the bench is set up. It is a dated status
note, not one of the three documentation planes; the requirements live in the
[FSD](docs/Functionality/TDisplay-S3-Webserver-FSD.md) and the how-to in the
[User Manual](docs/UserDocumentation/User-Manual.md).

As of 2026-11-01, 7:45 PM EST.

## What the device is

A small WiFi weather logger built on the LilyGO T-Display-S3 AMOLED 1.91"
(ESP32-S3 with a 240×536 RM67162 AMOLED over QSPI), running ESP-IDF v6.0.2.
It reads a BME280 (temperature, humidity, pressure), shows the readings on the
screen, keeps a history in flash, serves a status page and JSON API on the
LAN, and emails a periodic summary. Between readings it sleeps.

It was cloned from `solchjg/tdisplay-s3-webserver` and the display layer was
moved to the AMOLED. Reusable parts (WiFi, portal, HTTP, OTA, BLE setup,
BME280, mail, flash ring, the RM67162 driver and board constants, and more)
live in [`solchjg/esp-idf-components`](https://github.com/solchjg/esp-idf-components),
checked out here as the `shared/` submodule. Each change follows the loop in
[`docs/Method/AI-Workflow.md`](docs/Method/AI-Workflow.md): requirement in the
FSD first, then code, then a test that passes on the host or the bench.

## Features on `main`

| Feature | FSD | What it does |
|---|---|---|
| WiFi and provisioning portal | §5, §13 | Joins the LAN; with no credentials it opens an access point with a captive setup page |
| Status page and API | §14 | `GET /` and `GET /api/status` on port 80, plus mDNS name and a UDP log mirror |
| OTA with rollback | §15 | Downloads new firmware from a URL; an image that never reaches the network is rolled back |
| Factory reset | §18 | Holding BOOT 5 s erases settings (and the history) and returns to the portal |
| Deep sleep and time sync | §21, §22 | Sleeps between readings; local time from SNTP while WiFi is up |
| BME280 weather sensor | §23 | Readings on screen line 4, in the log and at `/api/readings`; kept across sleep |
| Setup check | §24 | After setup the device holds awake until a key press confirms it |
| Summary email | §25 | SMTP over TLS on a schedule; WiFi turns on during a timer wake only when a summary is due |
| BLE setup | §26 | First setup and later changes from a phone through a Web Bluetooth page, with a passkey shown on the screen; opened by the portal or a 3 s user-key hold |
| Reading history | §27 | Every reading in a 1.5 MB flash partition (about 15 months at 10 min); CSV at `/api/history?from=&to=` and download over BLE |

## In flight

### Code PRs

- **Sampling setups** ([#21](https://github.com/solchjg/tdisplay-s3-amoled/pull/21), FSD §28):
  selectable sampling setups with a timed quick test and a sleep-cycle test. Bench
  passed (quick 12/12, cycle 20/20, email sent). Waiting for Joe to review and merge;
  merge it first, since #24 contains it.
- **BLE phone reconnect** ([esp-idf-components #11](https://github.com/solchjg/esp-idf-components/pull/11),
  [#24](https://github.com/solchjg/tdisplay-s3-amoled/pull/24)): Android reconnects
  after pairing. The board keeps up to 3 pairings, a "Forget all phones" button,
  GATT client on to answer the phone's connect notice. Next: flash build
  0.0.0-blefix6, phone test, then merge #11, re-pin `shared/`, merge #24.
  Details in `notes/ble-reconnect-status.md` in the project files.
- **Setup form checks** ([esp-idf-components #12](https://github.com/solchjg/esp-idf-components/pull/12),
  [#25](https://github.com/solchjg/tdisplay-s3-amoled/pull/25), FSD 0.18.0): the
  portal and BLE page check fields before sending and keep what was typed. CI
  green; waits for #11 and #24, then a bench check.

### Requirement PRs (docs only, waiting for Joe's approval before any code)

These are stacked: #21 ← #22 ← #23 ← #26.

- **4-20 mA loop input** ([#22](https://github.com/solchjg/tdisplay-s3-amoled/pull/22), FSD §29):
  an optional isolated current-loop input. Each add-on is turned on or off on its
  own, and one that is off leaves no trace. Open point: should a test fail when an
  enabled add-on gives no value (drafted as yes).
- **RS232 instrument input** ([#23](https://github.com/solchjg/tdisplay-s3-amoled/pull/23), FSD §30):
  read an instrument over RS232 through a through-hole MAX3232 powered from
  GPIO 13 (UART1 on GPIO 11/12), a panel-mount DE-9 or a screw terminal; circuit
  in `docs/Method/project/rs232-circuit.md`. Still proposed: the field limits,
  a 10 ms settle and the optional RTS line.
- **ntfy push alerts** ([#26](https://github.com/solchjg/tdisplay-s3-amoled/pull/26), FSD §31):
  alerts when a reading crosses a limit and returns, when the sensor, an add-on or
  the history fails or recovers, and when a test ends. Test server
  `http://192.168.2.231`.

### Plans (no PR yet)

- **ThingsBoard telemetry** (would be FSD §32): upload readings in batches over
  HTTPS with their own time stamps, from the flash history, with an "uploaded up
  to" mark so nothing is lost offline. New shared `tb_tx` component. Waiting for
  Joe: upload timing, Cloud or self-hosted, HTTP or MQTT, where alerts live.
- **PCB** ([proposal](https://claude.ai/artifact/27K3UtacXWAmqMVtgiSymw)): a
  2-layer carrier board, about 70 × 40 mm, that the T-Display plugs into, with the
  BME280 on a slotted tongue, LiPo socket and switch, current-measuring jumper,
  Qwiic and an expansion header. Open: the user key (GPIO 21) and BOOT are not on
  the headers; battery life (about 3.5 months on 1000 mAh) is an estimate until
  sleep current is measured.

### Bench fix upstream

- **Bench reset on wake** ([SensorsIot/Embedded-AI-Harness #64](https://github.com/SensorsIot/Embedded-AI-Harness/pull/64)):
  the bench reopened the serial port about 2 s after each USB reconnect and briefly
  held the board in reset. The fix (probe once per board, and open the port without
  toggling reset) is installed on the bench Pi and verified with a `std,cycle` run
  on SLOT1. The PR's description still needs updating before it goes for review,
  from Joe's laptop session (it is outside this project's GitHub access). Install
  steps are in `notes/bench-fix-install.md` in the project files.

## Bench setup

- Embedded AI Harness testbench at `http://192.168.2.230:8080`, slot **SLOT1**,
  serial `rfc2217://192.168.2.230:4001`.
- Before any run, check `detected_chip: esp32s3` and MAC `48:27:e2:ea:48:3c`;
  the slot can hold another project's board.
- Flash and reset over USB only (EN/BOOT GPIOs are not proven wired). Flash all
  four images (bootloader 0x0, partition table 0x8000, ota_data_initial 0xf000,
  app 0x20000); OTA cannot add the history partition.
- Never open the USB serial port with DTR/RTS asserted; use the bench's serial
  monitor. A sleeping board is off USB: press the user key or reset and flash
  within about 60 s.
- BME280 on SDA GPIO 43 / SCL GPIO 44. Leave the rig with the shipped build
  flashed.
- Tagged releases (`v*.*.*`) flash the built firmware and run the release journey
  on the self-hosted `testbench` runner before publishing.
