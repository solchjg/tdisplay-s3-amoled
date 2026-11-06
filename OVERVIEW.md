# Project overview

A plain-language snapshot of the whole project: what the device is, what works
on `main`, what is being tried, and how the bench is set up. It is a dated status
note, not one of the three documentation planes; the requirements live in the
[FSD](docs/Functionality/TDisplay-S3-Webserver-FSD.md) and the how-to in the
[User Manual](docs/UserDocumentation/User-Manual.md).

As of 2026-11-05, 10:00 PM EST.

## What the device is

A small weather logger built on the LilyGO T-Display-S3 AMOLED 1.91"
(ESP32-S3 with a 240×536 RM67162 AMOLED over QSPI), running ESP-IDF v6.0.2.
It reads a BME280 (temperature, humidity, pressure) and, optionally, a 4-20 mA
loop, shows the readings on the screen, keeps a history in flash and sleeps
between readings. It reports over WiFi (status page, JSON API, summary email,
ntfy push alerts) or, with WiFi off or out of reach, through an optional Blues
Notecard over cellular.

It was cloned from `solchjg/tdisplay-s3-webserver` and the display layer was
moved to the AMOLED. Reusable parts (WiFi, portal, HTTP, OTA, BLE setup,
BME280, ADS1115, mail, ntfy, Notecard, flash ring, the RM67162 driver and
board constants, and more) live in
[`solchjg/esp-idf-components`](https://github.com/solchjg/esp-idf-components),
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
| Deep sleep and time sync | §21, §22 | Sleeps between readings; local time from SNTP while WiFi is up (or from the Notecard) |
| BME280 weather sensor | §23 | Readings on screen line 4, in the log and at `/api/readings`; kept across sleep |
| Setup check | §24 | After setup the device holds awake until a key press confirms it |
| Summary email | §25 | SMTP over TLS on a schedule; WiFi turns on during a timer wake only when a summary is due |
| BLE setup | §26 | First setup and later changes from a phone through a Web Bluetooth page, with a passkey shown on the screen; opened by the portal or a 3 s user-key hold |
| Reading history | §27 | Every reading in a 2 MB flash partition; CSV at `/api/history?from=&to=` and download over BLE |
| Sampling setups | §28 | `std`, `fine`, `slow` or `custom` periods, plus timed `quick`, `cycle` and `gps` tests with a pass/fail verdict |
| 4-20 mA loop input | §29 | Optional isolated loop on its own ADS1115; its value on line 6, in every reading and in the history |
| Push alerts | §31 | ntfy alerts when a reading crosses a limit and returns, when a part fails or recovers, and when a test ends |
| Cellular uplink | §33 | Alerts and email as Notecard notes routed by Notehub; clock and a GPS place from the Notecard |
| WiFi off | §34 | Runs on Bluetooth alone; reports only through the cellular uplink |

## Specified, not built

- **RS232 instrument input** (§30): add-on slot 1, through a MAX3232 on UART1;
  circuit in [`rs232-circuit.md`](docs/Method/project/rs232-circuit.md).
- **ThingsBoard telemetry** (§32): batched uploads from the history. The server
  guides ([ThingsBoard Setup](docs/UserDocumentation/ThingsBoard-Setup.md),
  [Cloud Server](docs/UserDocumentation/Cloud-Server.md)) are written; the
  firmware and the cloud server are not built yet.
- **Direct LTE-M modem** (§35, draft): a build for the LilyGO T-SIM7080G-S3
  over PPP, evaluated in [`lte-m-modem.md`](docs/Method/project/lte-m-modem.md).

## Trials and bring-up (no FSD section yet)

- **Satellite data** (`commissioning/sat-probe`): a compact `day.qo` note of 24
  hourly loop averages sent through the Notecard, decoded by a Notehub route
  ([`route.jsonata`](commissioning/sat-probe/route.jsonata)) into ThingsBoard.
  Stage 2 (over `cell`) passed; stage 3 (over Skylo `ntn`) is next.
- **E-paper** (`commissioning/epaper-probe`): an Elecrow CrowPanel 4.2" as an
  alternative screen, in [`crowpanel-epaper.md`](docs/Method/project/crowpanel-epaper.md).
- **LoRaWAN site survey**: lives in [`solchjg/lora`](https://github.com/solchjg/lora).
- **PCB**: a 2-layer carrier board the T-Display plugs into
  ([proposal](https://claude.ai/artifact/27K3UtacXWAmqMVtgiSymw)); wiring of every
  subsystem is in [`wiring.md`](docs/Method/project/wiring/wiring.md).

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
- BME280 and Notecard on SDA GPIO 43 / SCL GPIO 44. Leave the rig with the
  shipped build flashed and the DUT ONLINE on the bench AP.
- Tagged releases (`v*.*.*`) would flash the built firmware and run the release
  journey on a self-hosted `testbench` runner before publishing; no such runner
  is registered for this repo yet, so no tag can be released.
