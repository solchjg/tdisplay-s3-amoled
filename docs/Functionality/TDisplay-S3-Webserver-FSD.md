# T-Display-S3 AMOLED Webserver — Functional Specification Document (FSD)

This document is the **WHAT** plane: what must be true of the firmware. The plane
map is [`../00-Overview.md`](../00-Overview.md); the other planes are
[`../Method/`](../Method/00-Overview.md) (HOW) and the
[User Manual](../UserDocumentation/User-Manual.md) (OPERATE).

**Conventions.** Requirements are `FR-x.y` (functional), `NFR-x.y` (quality) and
`C-x.y` (constraint), each with a priority `[Must/Should/May]` and a provenance tag:
`[user]` stated or approved by the user; `[derived]` a logical consequence of a
`[user]` item or a board fact; `[pack:esp32]` proposed by the ESP32 domain pack and
accepted by the user. A number marked is a value this document
proposes that the user has not yet accepted; every one is listed under *Open
decisions* (Appendix B). Everything else is approved.

**Stable IDs.** This project started from `tdisplay-s3-hello`. Requirement IDs and
chapter numbers carried over from that base keep their meaning, and an id whose
meaning changes is superseded by a new one (Appendix B); chapters added for
the webserver are numbered from 13 upward and placed in the Part their layer
belongs to, so chapter numbers are identifiers, not reading order. Base
requirements that no longer apply are listed as superseded in Appendix B, never
reused.

## Contents

| § | Section | Layer |
|---|---------|-------|
| 1 | System Overview | |
| 2 | System Architecture (incl. 2.4 Component Layering) | |
| 3 | Implementation Phases | |
| 4 | Risks, Assumptions & Dependencies | |
| 5 | Device Logic: connectivity and display state | L2 |
| 6 | LCD Display Interface | L1 |
| 7 | Serial Log Interface | L1 |
| 13 | Provisioning Portal | L1 |
| 14 | Status HTTP Server | L1 |
| 15 | OTA Receiver | L1 |
| 16 | UDP Log | L1 |
| 17 | mDNS Responder | L1 |
| 18 | BOOT Button | L1 |
| 8 | ESP-IDF Platform | L0 |
| 9 | Security | cross-cutting |
| 10 | Build & Platform Constraints | cross-cutting |
| 19 | Watchdog | cross-cutting |
| 20 | Device Identity | cross-cutting |
| 21 | Deep Sleep | cross-cutting |
| 22 | Time | cross-cutting |
| 23 | Weather Sensor | L1 |
| 24 | Setup Check | cross-cutting |
| 25 | Summary Email | L1 |
| 26 | BLE Setup | L1 |
| 27 | Reading History | L1 |
| 28 | Sampling Setups | L1 |
| 29 | 4-20 mA Loop Input | L1 |
| 30 | RS232 Instrument Input | L1 |
| 31 | Push Alerts | L1 |
| 32 | ThingsBoard Telemetry | L1 |
| 33 | Cellular Uplink | L1 |
| 34 | WiFi Off | cross-cutting |
| 35 | Cellular Build | cross-cutting |
| 36 | E-paper Display (optional) | L1 |
| 37 | Packed Messages | L1 |
| 38 | ESP-NOW Link (optional) | L1 |
| 39 | Hardware Clock (optional) | L1 |
| 40 | Low-Power Build (FeatherS3) | cross-cutting |
| 41 | Satellite Uplink (optional) | L1 |
| 42 | Remote Session (optional) | L1 |
| 11 | Operational Procedures | operations |
| 12 | Verification & Validation | operations |
| A | Constants & Configuration Catalogue | |
| B | Lifecycle Metadata (incl. superseded requirements, open decisions) | |

---

## 1. System Overview

Firmware for the LilyGO T-Display-S3 AMOLED (ESP32-S3 with a built-in 1.91-inch
RM67162 240×536 AMOLED) that joins an existing WiFi network as a station and serves a status
page and a JSON status API over plain HTTP. The built-in LCD mirrors the device's
IP address, its hostname, and the number of HTTP requests it has answered.

A device with no stored WiFi credentials opens its own open access point with a
captive provisioning portal; credentials entered there are stored in flash and
used from then on. A long press of the on-board BOOT button erases them and
returns the device to the portal. The same settings can also be entered, and
later changed, from a phone over Bluetooth Low Energy (§26), in a short window
that the portal or a 3 s hold of the user key opens. New firmware is installed by telling the
device, over HTTP, a URL to download it from; an image that does not reach the
network after its first boot is rolled back automatically. WiFi can be
switched off in the settings (§34): the device then keeps its readings,
history and sleep, reports over cellular when that uplink is chosen, and is
set up and read over BLE alone.

**Goals.** A network-attached status device that can be provisioned, observed,
updated and recovered without a USB cable, and whose every behaviour can be
verified on the Harness testbench.

**Non-goals.** Authentication, TLS, BLE outside a setup window (§26), MQTT, a WiFi network list in the portal,
static IP addressing, IPv6, time synchronisation, OTA by pushed image body,
HTTPS downloads, more than one stored network, touch, the second user button (GPIO 14), battery measurement,
PSRAM use, brightness dimming.

**Users.** Someone on the trusted LAN who opens the status page; the person who
provisions and updates the device; the developer running the AI closed loop on
the testbench, with a bench operator for the display and button checks.

**High-level flow.** Reset → start the LCD → no credentials: open the portal and
wait for credentials | credentials: connect → online: serve status page, API and
OTA, answer mDNS, mirror logs over UDP, show IP and request count → WiFi lost:
retry with backoff until it returns → OTA request: download, restart into the new image,
which confirms itself on reaching the network or is rolled back → BOOT held 5 s:
erase credentials and return to the portal.

---

## 2. System Architecture

### 2.1 Logical Architecture

```
                    ┌─────────────────────────────── ESP32-S3 ───────────────────────────────┐
 BOOT button ──────►│ BOOT Button (§18) ──┐                                                    │
                    │                     ▼                                                    │
                    │  WiFi/IP events ─► Device Logic (§5) ─► LCD Display Interface (§6) ──────┼─ QSPI ► RM67162
                    │  1 s tick ──────►   │    ▲   │                                           │
                    │                     │    │   └──► Serial Log (§7) ────────────────────────┼─ USB-Serial/JTAG ─► bench
                    │                     │    │        └─► UDP Log (§16) ──────────────────────┼─ UDP :5555 ─► gateway
                    │                     ▼    │                                                │
                    │  Provisioning Portal (§13) ◄── SoftAP clients (PROVISIONING only) ──────┼─ WiFi AP
                    │  Status HTTP Server (§14) ◄─── LAN clients (ONLINE only) ───────────────┼─ WiFi STA
                    │  OTA Receiver (§15) ◄── LAN clients; ──► image server (ONLINE only) ────┤
                    │  mDNS Responder (§17) ◄──────── LAN (ONLINE only) ──────────────────────┘
                    └──────────────────────────────────────────────────────────────────────────┘
```

The Device Logic decides; every interface reports events to it and executes the
actions it returns. BLE Setup (§26) is one more such interface, running only
inside a window the Device Logic opens. The WiFi radio is in exactly one role at a time: SoftAP in
PROVISIONING, station otherwise. The display path is independent of the network
path: a failing display never stops the network side (FR-5.28).

### 2.2 Hardware / Platform Architecture

| Item | Value | Source |
|------|-------|--------|
| Board | LilyGO T-Display-S3 AMOLED 1.91" | [user] |
| MCU | ESP32-S3R8: 16 MB flash, 8 MB octal PSRAM (PSRAM unused) | [derived from the board] |
| Display | RM67162 AMOLED controller, 240×536 visible pixels, RGB565, QSPI (4 data lines) | [derived from the board] |
| LCD data pins | QSPI D0 – D3 = GPIO 18, 7, 48, 5 | [derived from the LilyGO T-Display-S3 AMOLED pin map] |
| LCD control pins | SCK = GPIO 47, CS = GPIO 6, RST = GPIO 17 (TE = GPIO 9, unused) | [derived from the LilyGO T-Display-S3 AMOLED pin map] |
| LCD power enable | GPIO 38 (active high; panel power on the touch board, an LED on the non-touch board) | [derived from the LilyGO T-Display-S3 AMOLED pin map] |
| Brightness | No backlight: register 0x51 of the RM67162, 0 = dark | [derived from the board] |
| QSPI clock | 75 MHz requested (the SPI divider runs it at 80 MHz) | [user] |
| Orientation | MADCTL 0x60 (MX, MV): landscape 536×240, no RAM offset | [derived from commissioning] |
| BOOT button | GPIO 0, active low, external pull-up on the board | [derived from the LilyGO T-Display-S3 AMOLED pin map] |
| Radio | ESP32-S3 2.4 GHz WiFi (802.11 b/g/n); BLE 5 used only inside a setup window (§26, FR-26.18), sharing the radio by ESP-IDF software coexistence; ESP-NOW, optionally in LR mode, with `espnow_role` not `off` (§38) | [derived from the board] |
| Power and console | USB-C on the board, which is the ESP32-S3's native USB-Serial/JTAG | [derived from the board] |
| Flash layout | Two 4 MB application slots, see C-10.6 | [user] |
| 4-20 mA loop (optional) | One 2-wire transmitter on an isolated loop side: 24 V isolated supply from the battery/system rail behind a soft-start load switch, enabled by GPIO 3, a 30 mA loop current limit, 100 Ω shunt, ADS1115 at 0x49 (128 SPS) on an isolated 3.3 V LDO, behind an I2C isolator on its own bus (GPIO 15/16), the isolator's board side powered from GPIO 3 (§29) | [user] |
| RS232 instrument (optional) | One instrument on a DE-9 (DTE) or screw terminal: MAX3232 transceiver (PDIP-16) powered from GPIO 13, UART1 TX GPIO 11, RX GPIO 12 (§30); circuit in `docs/Method/project/rs232-circuit.md` | [user] |
| Cellular (optional) | Blues Notecard Cell+WiFi NOTE-MBGLW v2.5 on a Notecarrier-X v1.3, I2C 0x17 on GPIO 43/44 through Qwiic or wires soldered to its header, its own LiPo for the modem, run on cellular only (§33) | [user]; bus and power [proposed] |
| Add-on boards | The loop (§29) and the RS232 circuit (§30), and later the water probe, are each built on a separate board that plugs into a carrier holding the T-Display-S3 and the BME280; the Notecarrier-X (§33) is a board of its own. Each analog add-on carries its own ADS1115, so the board pins carry only power, switch lines and digital buses, and any add-on can be fitted or left off without changing the others | [user] |
| Add-on connectors | One keyed through-hole JST-XH (2.5 mm) socket on the carrier for each add-on, each with a different pin count so a board fits only its own socket, pin 1 GND: loop 5 pins (GND, VSYS, GPIO 3, GPIO 15, GPIO 16), water probe 6 pins (GND, VSYS, 3V3, GPIO 43, GPIO 44, GPIO 2), RS232 4 pins (GND, GPIO 13, GPIO 11, GPIO 12); each add-on's pull-down on its switch line sits on the add-on; the Notecarrier-X on a Qwiic (JST-SH) socket of the carrier's I2C fan-out; boards are fitted or removed only with USB and battery disconnected | [proposed] |
| Qwiic | Qwiic wherever a part needs only 3.3 V, GND and the I2C lines [user]: the carrier's I2C fan-out is a Qwiic star adapter (e.g. SparkFun Qwiic MultiPort) on the BME280's bus (GPIO 43/44), into which the BME280, on a Qwiic breakout, and the Notecarrier-X (its red 3.3 V wire unconnected, §33.2) plug. The loop (its own bus and switched supply), the water probe (VSYS and its switch line) and RS232 (not I2C) keep their JST-XH sockets. One board's pull-ups stay in and the others' are cut, so the bus's pull-ups stay at or above about 2.2 kΩ | [user] |
| Bench slot | SLOT1 (EN/BOOT GPIOs configured on the bench but not yet proven; reset over USB) | [derived from the testbench device list, 2026-10-29] |

The numbers a build reads (target, IDF version, flash size, partition offsets, image
size limit, rollback, console) are stated as constraints in §10.

### 2.3 Software Architecture

- **Tasks.** One application task executes the Device Logic: it receives events
  (WiFi/IP, portal submissions, OTA results, button, 1 s tick) from a queue and
  executes the returned actions. The HTTP server runs in its own task. Both are
  watched by the task watchdog (§19).
- **Boot.** Bootloader → the application slot selected by `otadata` → LCD start →
  read credentials from NVS → PROVISIONING or CONNECTING (§5.3).
- **Persistence.** Only the WiFi credentials persist (NVS, Appendix A). Everything
  else — state, request counter, retry count — restarts at boot.
- **Update model.** HTTP download from a given URL into the non-running slot (§15); restart into it;
  the new image confirms itself on reaching ONLINE, otherwise the bootloader rolls
  back on the next restart (FR-5.23, FR-5.24, C-10.8).

### 2.4 Component Layering

```
┌──────────────────────────────────────────────────────────────────────────┐
│ L2  Application logic │ Device Logic (§5): connectivity state machine,  │
│                       │ display state and screen content                 │
├──────────────────────────────────────────────────────────────────────────┤
│ L1  Interfaces        │ LCD Display (§6) · Serial Log (§7) ·             │
│                       │ Provisioning Portal (§13) · Status HTTP (§14) ·  │
│                       │ OTA Receiver (§15) · UDP Log (§16) ·             │
│                       │ mDNS Responder (§17) · BOOT Button (§18) ·       │
│                       │ Deep Sleep (§21) · BLE Setup (§26)               │
├──────────────────────────────────────────────────────────────────────────┤
│ L0  Foundation        │ ESP-IDF (§8): esp_wifi, esp_netif/lwIP,          │
│                       │ esp_http_server, nvs_flash, app_update,          │
│                       │ bootloader rollback, esp_task_wdt, esp_lcd,      │
│                       │ GPIO, esp_timer, FreeRTOS, esp_log;              │
│                       │ managed espressif/mdns; NimBLE (bt)              │
└──────────────────────────────────────────────────────────────────────────┘
                dependencies point strictly downward
```

- **L2 Device Logic** owns both state models of §5, the screen content of each
  state, the retry schedule, the confirm-or-roll-back decision, and when the
  task runs (§21). Pure
  functions; no I/O.
- **L1 interfaces** own their wire formats: the HTTP routes and bodies, the portal
  form and its validation, the OTA request and download rules, the log line formats, the
  datagram format, the mDNS name, the button's hold timing, and the panel.
  Each one's pure core (validation, formatting, escaping, schema rendering) is
  separable for the host tier.
- **L0 ESP-IDF** is configured and used, not implemented; tested transitively.

Source layout and dependency rules that implement this layering are HOW:
[`../Method/project/layout.md`](../Method/project/layout.md).

---

## 3. Implementation Phases

Each phase can be entered using only what an earlier phase delivered, plus the
testbench.

### 3.1 Phase 1: Platform

- **Scope.** New partition table and rollback-enabled bootloader, LCD carried over,
  the boot screen, the new serial log lines, task watchdog. No WiFi.
- **Deliverables.** C-10.1 – C-10.3, C-10.5 – C-10.8; §6 (FR-6.x, C-6.x) against
  the BOOT screen; FR-5.25 (BOOT row), FR-5.27, FR-5.29, FR-5.31 (core),
  FR-7.5 – FR-7.7, FR-7.12, FR-7.13, FR-7.34, FR-7.29, FR-7.30 (image check); FR-19.1,
  NFR-19.1 (variant app); the LCD fault build and the hang build
  (variant `app`) of §12.0.
- **Exit criteria.** On SLOT1, after a USB flash of the CI artefact and a USB
  reset: the boot line of FR-7.7 and `Init complete, running event-driven` appear;
  the LCD shows `Starting` (operator); `idf.py size` fits C-10.7; the
  watchdog hang build resets with reason `TASK_WDT`.
- **Dependencies.** Testbench USB flash, serial monitor and operator prompt; the
  commissioned panel (`lcd-rm67162`). Nothing from a later phase.

### 3.2 Phase 2: Provisioning and connectivity

- **Scope.** §5 connectivity state machine, the portal (§13), credential storage,
  reconnect with backoff, the screen content of every state.
- **Deliverables.** FR-5.11 – FR-5.20, FR-5.25, FR-5.30, NFR-5.1 – NFR-5.3; §13;
  FR-7.8 – FR-7.10, FR-7.17 – FR-7.24, FR-7.30 (wire); FR-9.3 (serial part), FR-9.4.
- **Exit criteria.** From an erased NVS, the testbench `enter-portal` procedure
  provisions the device through `POST /connect`; the device joins the bench AP and
  logs `web: online`; after a reset with the bench AP down and a fresh AP start it
  rejoins without the portal (a reset while the AP runs is blocked by bench fault
  SensorsIot/Embedded-AI-Harness#54); after the bench AP is stopped and restarted
  it rejoins within NFR-5.2.
- **Dependencies.** Phase 1 (serial log is the evidence channel); testbench WiFi
  AP, station join, HTTP relay and WiFi scan.

### 3.3 Phase 3: Status server

- **Scope.** Status page, JSON API, request counter, UDP log, mDNS, identity,
  HTTP-handler watchdog, display failure under network load.
- **Deliverables.** §14, §16, §17, §20, FR-5.26, FR-5.28, FR-5.31 (bench), FR-7.14, FR-7.26, FR-9.3 (UDP and
  HTTP parts), FR-9.5, FR-19.2, NFR-19.1 (variant http); the hang build variant `http`.
- **Exit criteria.** Through the bench HTTP relay: `GET /` and `GET /api/status`
  answer per §14 with consistent values; the LCD request count follows (FR-5.25,
  operator plus FR-7.34 mirror); NFR-14.1 holds; only TCP port 80 is open.
- **Dependencies.** Phase 2 (the device must be ONLINE on the bench AP); Phase 1's
  test builds; testbench UDP log capture and port scan.

### 3.4 Phase 4: OTA and factory reset

- **Scope.** OTA download, confirm and rollback; BOOT-button factory reset.
- **Deliverables.** §15, §18, FR-5.21, FR-5.23, FR-5.24, FR-5.32, FR-7.16, FR-7.25, FR-7.31, FR-7.32, FR-7.27, FR-7.28 (§15 includes FR-15.13); the test build
  §12.0 names for rollback.
- **Exit criteria.** A valid image hosted on the bench and requested through
  `POST /ota` boots and confirms; an invalid, oversized or unreachable image
  fails without restart; the
  no-network test image is rolled back; an operator-held BOOT button returns
  the device to the portal.
- **Dependencies.** Phase 3 (the station HTTP server carries `POST /ota`);
  Phase 2 (portal and NVS for the factory reset); bench firmware hosting
  (`/api/firmware/upload`, served at `/firmware/<project>/<file>`) reachable from
  the bench AP network (R-6).

### 3.5 Phase 5: Weather sensor

- **Scope.** The BME280 read as the deep-sleep task, its readings logged,
  shown, served and kept across sleep.
- **Deliverables.** §23; the shared `bme280` component; the sensor
  fault build §23.4 names.
- **Exit criteria.** A reset shows a plausible L21 before the window; three
  timer wakes each log L21 and `/api/readings` lists them; the panel's line 4
  and the page match the latest L21; a sensor at the wrong address logs L22
  and the device carries on.
- **Dependencies.** §21 (the task and its sleep), §22 (the time stamp); a
  BME280 wired to SDA GPIO 43 / SCL GPIO 44 of the board in SLOT1.

### 3.6 Phase 6: Setup check

- **Scope.** Five 33-character screen lines, the `Hum` reading line, and the
  hold after WiFi is configured.
- **Deliverables.** §24, FR-6.12, FR-6.13, FR-7.34, FR-23.12; the sleep test
  builds' 60 s hold timeout (§24.3).
- **Exit criteria.** Provisioning logs L23 and the device does not sleep; the
  panel counts down on line 5; a key press logs L24 `by=key` and the device
  sleeps after the window; the sleep test build times out and holds again on
  the next key wake.
- **Dependencies.** §5 (T3), §21 (the key and the window), §23 (the readings).

### 3.7 Phase 7: Summary email

- **Scope.** Mail settings in the portal, the due-time rule, the summary text,
  SMTP over TLS, and WiFi on a timer wake only for a due summary.
- **Deliverables.** §25, FR-21.13, L25, L26; the shared `mail_tx` component.
- **Exit criteria.** A WiFi-only submission still provisions; a full one stores
  the settings; at the first due time the recipient gets the summary and the
  device logs L25 and sleeps; an unreachable server logs L26 and the device
  sleeps; timer wakes with no summary due keep WiFi off.
- **Dependencies.** §13 (the form), §21 (the task and its sleep), §22 (local
  time), §23 (the kept readings).

### 3.8 Phase 8: BLE setup

- **Scope.** The shared `ble_setup` component and the form core it shares with
  the portal, the provisioning and key windows, passkey pairing on the panel,
  settings changed over BLE, and the Web Bluetooth page.
- **Deliverables.** §26, FR-25.16, T3 from BLE, T14, L27 – L30.
- **Exit criteria.** From an erased NVS, a BLE central provisions the device
  with the passkey from the panel and it reaches ONLINE; the portal still
  provisions; a 3 s user-key hold opens a key window, a settings write is
  stored and the device restarts with it; the window closes after 5 min idle;
  no advertiser outside a window; `free_heap` returns after a window.
- **Dependencies.** §13 and §25.2 (the form and its rules), §21 (the user key
  and the window), §24 (the hold a key press ends); a testbench BLE central
  that can pair with a passkey (R-24).

### 3.9 Phase 9: Reading history

- **Scope.** The `history` partition, appending each reading to the shared
  `flash_ring`, the CSV core, `GET /api/history`, the BLE `history`
  characteristic and the page's download.
- **Deliverables.** §27, C-10.9, L31, R-26.
- **Exit criteria.** Readings from timer wakes appear in `GET /api/history`;
  the history survives a power cycle; the BLE download equals the HTTP one;
  a factory reset empties it; an app-only flash over the old table runs
  without history.
- **Dependencies.** §23 (the readings), §26 (the key window and service),
  §14 (the HTTP server), §18 (factory reset).

### 3.10 Phase 10: Sampling setups

- **Scope.** The setup list and its selection in the portal and over BLE,
  the `quick` and `cycle` tests, the double press, the verdict on line 5 and
  the result email.
- **Deliverables.** §28, FR-23.17, the FR-21.13 exception, L32, L33.
- **Exit criteria.** A BLE write of `setup=cycle,std` runs 20 readings over
  timer wakes and ends with `Test pass 20/20`; a double press runs `quick`;
  a key press ends a test as failed; existing settings keep their periods.
- **Dependencies.** §23 (readings, averages), §21 (sleep, the key wake),
  §25 (mail), §26 (BLE settings), §27 (history).

### 3.11 Phase 11: 4-20 mA loop input

- **Scope.** The optional loop input, the add-on value in readings,
  averages, history, page, JSON and email, the sixth screen line, and the
  24-byte history record.
- **Deliverables.** §29, §29.2, FR-6.14, FR-6.15, FR-23.18, FR-27.11,
  C-10.10, L34 – L36; the shared `ads1115` and `loop420` components; the
  commissioning program `loop-probe` for the loop hardware, run before the
  first bench test of §29.
- **Exit criteria.** With a calibrator at 4, 8, 12, 16 and 20 mA, every L34
  value is within ±0.25 % of span; a fault current logs L35 and stops
  nothing; a shorted loop stays at or below 30 mA; the loop hardware draws
  nothing with the supply off; with `loop` `off` the loop leaves no trace; the value appears on line 6, the page, `ext`, the CSV and the
  summary; with `loop` `off` nothing changes; the old history is erased once.
- **Dependencies.** §23 (readings), §27 (history), §28 (tests), §25 (mail),
  §26 (BLE settings), the carrier PCB's loop island.

### 3.12 Phase 12: RS232 instrument input

- **Scope.** The optional RS232 input as add-on slot 1 of §29.2: poll or
  listen, one number from a text line, scaled, in readings, averages,
  history, page, JSON, line 6, tests and email.
- **Deliverables.** §30, L37, L38; the shared `serial_port` component; the
  through-hole circuit of `rs232-circuit.md`; a loopback commissioning
  program for GPIO 11 – 13; a USB-RS232 adapter on
  the testbench host for the bench tier (R-27).
- **Exit criteria.** A scripted instrument on the bench adapter is read at
  every reading in `poll` and `listen`; silence, garbage and a long line log
  L38 and stop nothing; with `rs` `off` RS232 leaves no trace and the
  sleep current is unchanged; the value appears on line 6, the page, `ext`,
  the CSV and the summary.
- **Dependencies.** §29.2 (add-on values), §23, §27, §28, §25, §26, the
  carrier PCB's RS232 block.

### 3.13 Phase 13: Push alerts

- **Scope.** The ntfy settings in the portal and over BLE, limit and failure
  alerts with hysteresis, the test alert, sending on a timer wake.
- **Deliverables.** §31, the FR-21.13 exception for a due alert, L39, L40,
  R-28; the shared `ntfy_tx` component in esp-idf-components.
- **Exit criteria.** With the user's test server at `http://192.168.2.231`
  a reading past a limit puts one message on the topic and the reading back
  inside it one more; an unreachable server logs L40 and the device still
  sleeps within 50 s; with `ntfy` `off` nothing changes and the settings stay.
- **Dependencies.** §23 (readings), §25 (the shared timer-wake WiFi start),
  §26 (BLE settings), §28 (test result), §29 and §30 (add-on failures).

### 3.14 Phase 14: ThingsBoard telemetry

- **Scope.** The shared `tb_tx` component (on the same HTTP POST helper as
  `ntfy_tx`, §31), the `telemetry_core` due rule and batching, the upload
  mark, the settings in the portal and over BLE, uploads on timer wakes and
  in windows.
- **Deliverables.** §32, the FR-21.13 exception, L41, L42, R-30.
- **Exit criteria.** Readings from timer wakes appear on the bench
  ThingsBoard with their times; readings taken while it is stopped arrive with
  the next upload; a timer wake that uploads is asleep within 50 s; an upload
  over `https://` reaches the user's production server.
- **Dependencies.** §27 (history), §21 (sleep and timer wakes), §25 (the shared
  WiFi start), §26 (BLE settings), §22 (time); a ThingsBoard server on the
  bench network (R-30).

### 3.15 Phase 15: Cellular uplink

- **Scope.** The optional Notecard uplink: settings, Notecard setup, the
  ThingsBoard batches, alerts and summary email as notes, the clock from the
  Notecard.
- **Deliverables.** §33, L43 – L45, the L20 `src=cell` form, R-31 – R-33, R-35; the
  shared `notecard` component in esp-idf-components; a commissioning program
  that talks to the Notecard on GPIO 43/44 beside the BME280 and measures the time and size of a 100-reading `tb.qo` note and of one hourly sync (FR-33.7, NFR-33.2); a User Manual
  section for the Notehub project and its routes.
- **Exit criteria.** With `uplink` `cell` a timer wake adds its notes and is
  asleep within 10 s; the readings reach ThingsBoard, an alert reaches ntfy
  and a summary reaches the inbox through Notehub; with `uplink` `wifi`
  nothing changes.
- **Dependencies.** §32 (ThingsBoard batches and the upload mark), §31
  (alerts), §25 (summary), §22 (time), §26 (BLE settings), a NOTE-MBGLW and a
  Notecarrier-X with its LTE antenna at the bench (R-31).

### 3.16 Phase 16: WiFi off

- **Scope.** The `wifi` setting in the portal form and the BLE settings, the
  WIFI_OFF connectivity state, windows and timer wakes without WiFi, the
  senders with WiFi off.
- **Deliverables.** §34, the WIFI_OFF rows of §5.3 and §5.4, Appendix A; a
  User Manual section on running without WiFi.
- **Exit criteria.** Provisioned with `wifi` `off`, the device enters
  WIFI_OFF after every boot, never joins the bench AP or opens the SoftAP,
  sleeps and wakes as with WiFi on, and gives its settings and history over
  BLE; a key-window write of `wifi=on` brings it ONLINE; with WiFi on nothing
  changes.
- **Dependencies.** §5 (connectivity), §13 (portal form), §26 (BLE settings),
  §21 (windows and timer wakes), §33 (cellular uplink, for reporting without
  WiFi).

### 3.17 Phase 17: Cellular build

- **Scope.** The cellular build of §35 without the e-paper (Phase 18 adds it alongside): the board, the
  modem uplink over PPP with the modem powered down between sends, the clock
  and GPS from the modem,
  the OLED, the off-board key and its LED, settings from microSD, and every
  feature of §35.3 on the new pins.
- **Deliverables.** §35 except FR-35.2 and FR-35.15; a commissioning check of
  the pin map (R-36); L46, L47 and L49; a User Manual section for the build.
- **Exit criteria.** On SLOT2 the build passes the §35.6 contracts
  except the e-paper's and the JRN-01 journey's steps that apply to it; a 24 h
  soak on cellular delivers every ThingsBoard upload; the T-Display build is
  unchanged.
- **Dependencies.** §25, §31 – §34; the board's commissioning check of
  the modem, the pins and the sleep current.

### 3.19 Phase 19: ESP-NOW link

- **Scope.** The optional ESP-NOW link of §38 on both builds: node and
  gateway roles, LR mode, the gateway's forwarding to ThingsBoard's gateway
  API and to ntfy.
- **Deliverables.** §38, the FR-21.13 exception, the FR-34.8 exception, L57 –
  L61; the shared `espnow_link` component and a radio-only start in
  `wifi_net`; a gateway-API transport for ThingsBoard; a bench peer (a bare
  ESP32-S3 on a free slot); the commissioning probe and
  `docs/Method/project/espnow.md`; a User Manual section.
- **Exit criteria.** The §38.5 contracts pass on SLOT1 with the bench peer;
  the range test is recorded; with `espnow_role` `off` both builds behave
  exactly as before this phase.
- **Dependencies.** §37 (packed messages and the `pack` component), §21,
  §31, §32, §34; Phase 17 for the cellular build's roles.

### 3.18 Phase 18: E-paper display (optional)

- **Scope.** The optional e-paper display of §36. On the cellular build, now,
  beside Phase 17 and before the SSD1322 arrives: FR-35.2 and FR-35.15 with the
  e-paper wiring of §35.2, the `oled` setting and the status block (FR-35.24).
  On the T-Display build, after its other phases, with the rules defined then
  (FR-36.3).
- **Deliverables.** §36, FR-35.2, FR-35.15, FR-35.24, L48; the `epaper` and
  `oled` settings; an 8 × 16 font from `bdf_to_c.py`; a commissioning check
  of the panel on the Waveshare board (`commissioning/ws-epaper-probe`).
- **Exit criteria.** With `epaper` `on` and a panel fitted, each build shows
  its lines on the e-paper as its rules say; with `epaper` `off` both builds
  behave exactly as before this phase.
- **Dependencies.** On the cellular build, Phase 17's board, keys and
  sensor bus; on the T-Display build, every other phase of that build (FR-36.3).

### 3.20 Phase 20: Hardware clock (optional)

- **Scope.** The optional battery-backed RTC of §39 on both builds, and the
  phone's clock over BLE.
- **Deliverables.** §39, L55, L56, L20's `rtc` and `phone` sources; the
  shared `rv8803` component; a commissioning probe that measures the RTC's
  rate against the bench clock.
- **Exit criteria.** With an RTC fitted and set, a power cycle with no
  network gives readings with their time, marked synced; with none fitted,
  both builds behave exactly as before this phase.
- **Dependencies.** §22 (time), §26 (BLE setup), §33 and §35 (the cellular
  times). It waits for neither Phase 18 nor Phase 19.

### 3.21 Phase 21: Low-power build (FeatherS3)

- **Scope.** The FeatherS3 build of §40: the board, the switched supplies of the
  sensor bus, the displays and the field boards, the e-paper and OLED on the new
  pins, the 0 – 5 V level channel, the isolated serial port for the ultrasonic
  sensor, the surge-protected field entry, and the bench slot's RST and BOOT lines.
- **Deliverables.** §40, L62, L63; `CONFIG_APP_BOARD_FEATHERS3` and its `board.h`
  pins; the voltage channel in `loop` and its pure core; the serial port's RX
  inversion in `serial_port`; a commissioning program `commissioning/feathers3-probe`
  (the board's pins and buses, LDO2, the display supply and backfeed, the OLED's
  start-up time, the RST and BOOT lines, the sleep current part by part, the
  EALLT-1's supply current); a Method note for the analog board's voltage channel
  and the serial port; a User Manual section for the build.
- **Exit criteria.** On its bench slot the build passes the §40.6 contracts; the
  sleep current is recorded and accepted as NFR-40.1's limit; the other builds are
  unchanged.
- **Dependencies.** §29, §30, §35's display rules (Phase 18), §39; the hardware:
  the MB7569-100 and the EALLT-1 are on hand [user]; the FeatherS3, the displays'
  switch, the isolated converters and the surge protectors are awaited, and a
  bench slot is to be assigned. The probes' host cores and the §30 settings for the
  MB7569-100 can be checked before the board arrives.

### 3.22 Phase 22: Satellite uplink

- **Scope.** §41 on the T-Display and FeatherS3 builds: the Notecard for Skylo,
  the §37 `day` and `al` messages (version 2) over satellite, the monthly budget, and
  remote settings by Notehub environment variables.
- **Deliverables.** §41, L66, L67; the shared `pack` component of §37, if Phase 19 has not built it
  and its Notehub decoders; the satellite rules in `uplink_core`; a ThingsBoard
  rule chain forwarding the FR-41.10 attributes to Notehub, in the User Manual.
- **Exit criteria.** The §41.6 host rows pass; an outdoor operator run shows the
  messages in ThingsBoard and ntfy and a remote setup change taking effect.
- **Dependencies.** §32's remote settings, §33, §37; a Notecard for Skylo and
  Skylo coverage at the test site (R-49).

### 3.23 Phase 23: Remote session

- **Scope.** §42 on the Notecard builds: scheduling from ThingsBoard and
  satellite, the continuous session, its requests and the history upload.
- **Deliverables.** §42, L68, L69; the session rules in `uplink_core` (pure);
  the ThingsBoard widget and rule chain that add requests to Notehub, and the
  `hist` proxy route, in the User Manual.
- **Exit criteria.** The §42.4 host rows pass; an operator session answers in
  ThingsBoard and delivers a year's history to the route (NFR-42.1), within
  NFR-42.2's battery use.
- **Dependencies.** §32's remote settings, §33; §41 only for FR-42.2; an
  endpoint for the `hist` route (open decision).

---

## 4. Risks, Assumptions & Dependencies

### 4.1 Risks

| ID | Risk | Likelihood | Impact | Mitigation |
|----|------|-----------|--------|------------|
| R-1 | The serial mirror proves what the firmware *intended* to draw, not what the panel shows | Certain | Medium | Display content is confirmed by an operator (FR-5.25, FR-6.1, FR-6.3, FR-6.4); FR-7.34 makes the intent automatable |
| R-2 | The QSPI bus is write-only, so a real panel fault is invisible; the display-failure path can be exercised only by injecting a driver error | Certain | Low | Verified on the LCD fault-injection test build (§12.0) |
| R-3 | Boot deadlines are read from the device's own log timestamp, which counts from chip reset, not from the bench's reset command | Certain | Low | Accepted: the device clock is the only clock that sees both the reset and the line |
| R-4 | Wrong mirror, colour-inversion or offset settings produce an image every serial-only test misses | Medium | Medium | Operator checks FR-6.1, FR-6.3, FR-6.4 each name the defect to look for |
| R-5 | The bench slot may hold another project's board | Certain | Low | Every bench test first confirms the chip and MAC recorded in `testing/test-plan.yaml` |
| R-6 | The device downloads images from the bench's firmware route on port 8080; reachability from the bench AP network is the bench's declaration, not yet exercised by this project | Medium | Medium | Depended on as declared; if FR-15.10's first bench run disproves it, the fault is filed against the testbench, never worked around here |
| R-7 | The captive-DNS check (FR-13.5) assumes the bench's HTTP relay resolves names through the DUT's SoftAP DNS | Medium | Low | FR-13.5 is a Should; the bench does not declare this ability, so its bench test is blocked until it does (testbench change request, never a project workaround) |
| R-8 | The mDNS check (FR-17.1) assumes the bench Pi resolves `.local` names on its AP network | Medium | Low | FR-17.1 is a Should; the bench does not declare this ability, so its bench test is blocked until it does (testbench change request, never a project workaround) |
| R-9 | BOOT (GPIO 0) is not proven wired on SLOT1: factory reset can only be tested by an operator; holding BOOT while the chip resets enters the ROM download mode instead | Certain | Low | FR-18.x contracts are operator-tier and press only while the device is running |
| R-10 | Credentials that can never connect (wrong password, AP gone for good) leave the device retrying forever, because it never returns to the portal on its own (FR-5.19) | Certain | Medium | Accepted [user]: the BOOT-button factory reset (§18) is the recovery path |
| R-11 | The bench AP and the device SoftAP both use 192.168.4.1 | Certain | Low | The bench's provisioning procedure leaves the device SoftAP before it starts its own AP; the device's SoftAP is gone in every state but PROVISIONING (FR-9.4) |
| R-12 | The bench WiFi scan reports no BSSID | Certain | Low | SoftAP presence is matched by the device's own SSID (FR-13.1), which the device logs (FR-7.9) |
| R-13 | No peer on the bench AP network can stall a download (the bench declares no such mode), so the stall clause of FR-15.11 has no bench run | Certain | Medium | The stall timeout is ESP-IDF's HTTP client timeout (30 s), not a separate core; the stall clause is blocked until the testbench declares a stall mode (SensorsIot/Embedded-AI-Harness#55), never worked around in the project |
| R-14 | The bench AP does not re-admit a station that reset without a deauth (SensorsIot/Embedded-AI-Harness#54), so no test can reset or power-cycle a provisioned DUT and expect it back on the same AP | Certain | Medium | Waived for shipment (Appendix B, Verification waivers); a reset with the AP down and a fresh AP start is used wherever the requirement allows it |
| R-15 | The bench has one radio, which cannot scan while its AP beacons, so nothing can be scanned while the DUT is ONLINE on the bench AP or right after enter-portal starts the AP | Certain | Low | FR-9.4's ONLINE part and FR-5.13's SoftAP-gone timing are waived (Appendix B); their other parts are verified with the AP stopped |
| R-16 | The bench's UDP log listener cannot be stopped, so FR-16.3 (no receiver) has no bench run | Certain | Low | Waived (Appendix B); the non-blocking send holds it by construction |
| R-17 | The bench declares no current meter, so the sleep-current limit (NFR-21.1) has no bench run | Certain | Low | Measured by the operator with a meter, or waived; the key wake keeps the RTC peripheral domain off by construction |
| R-18 | The USB-Serial/JTAG port disappears while the chip sleeps, and EN/BOOT are not proven wired on SLOT1, so a sleeping device cannot be flashed or monitored | Certain | High | FR-21.3: every boot that is not a sleep wake stays awake 60 s; sleep tests use short periods; the user key wakes the device |
| R-19 | The bench AP may give the DUT no internet route, so SNTP (FR-22.1) may not be reachable from SLOT1 | Medium | Medium | Not yet observed; if the bench AP has no route, the bench test is blocked (testbench change request, never a project workaround) |
| R-20 | GPIO 43/44 are UART0 TX/RX: the ROM bootloader writes its boot banner on GPIO 43 (SDA) at every reset, which the sensor sees as stray START/STOP conditions | Certain | Low | No SCL clocks are sent, so no address can match; the driver installs the bus after the banner and the first transfer starts with a STOP |
| R-23 | The bench offers no SMTP server and its AP may not reach the internet, so a sent summary has no automatic observer | Certain | Medium | The SMTP core is host-tested against scripted replies; delivery is an operator check with the user's own account; an automatic target test waits for a testbench SMTP peer (testbench change request, never a project workaround) |
| R-21 | The bench has no reference thermometer, hygrometer or barometer, so accuracy is not verified, only plausibility and response | Certain | Low | The sensor's own accuracy (±1 °C, ±3 %, ±1 mbar per datasheet) is relied on; contracts check plausible ranges and a finger-warmth response |
| R-24 | The testbench's BLE tools (`ble_scan`, `ble_connect`, `ble_write`) may not complete LE Secure Connections passkey pairing, so paired steps may need an operator and a phone | Medium | Medium | Scans and the unpaired refusal (FR-26.12) run on the bench; paired steps run by operator prompt with the Web Bluetooth page until the bench declares passkey pairing (testbench change request, never a project workaround) |
| R-25 | BLE and WiFi share one 2.4 GHz radio and internal RAM (no PSRAM); a window slows WiFi and takes tens of KB of heap; NimBLE adds about 360 KB to the image (1.38 MB of the 4 MB slot, C-10.7) | Certain | Medium | BLE only inside windows (FR-26.18); no OTA download in a key window (FR-26.17); the task, and so the summary send, never runs in one (FR-26.5); NFR-14.1 is measured with no window open; NFR-26.1 checks the heap comes back |
| R-26 | OTA never rewrites the partition table, so a device updated only by OTA has no `history` partition | Certain | Low | FR-27.8: it runs without history and says so (L31); a USB flash of the full image adds the partition |
| R-30 | The bench offers no ThingsBoard server, so an upload has no automatic observer | Certain | Medium | The `tb_tx` and `telemetry_core` logic is host-tested; target tests need a ThingsBoard CE container on the bench network (testbench change request, never a project workaround); the production server is an operator check |
| R-22 | Bench tests that provision the DUT and then expect it to sleep after the window now meet the setup check hold, and SLOT1 cannot drive GPIO 21 | Certain | Medium | Tests that need sleep press the key by operator prompt, or run the sleep test builds, whose hold times out after 60 s; the shipped 30 min is a build constant, not a runtime value |
| R-28 | The testbench offers no ntfy server; the user's test server is at `http://192.168.2.231` (port 80) on the LAN, reached from the bench AP network; `192.168.2.37:8080` is ThingsBoard, not ntfy | Low | Medium | The cores are host-tested; the unreachable-server row runs on the target tier; if the bench AP stops reaching the server, the bench rows are blocked (testbench change request, never a project workaround) and run by the operator with the device on the user's own WiFi |
| R-31 | The testbench has no Notecard and no cellular coverage is declared at the bench, so the cellular rows need a Notecard on SLOT1's bus and a Notehub project | Certain | Medium | The user fits the Notecarrier-X by Qwiic at the bench; until then the cellular rows are `not done` and the cores are host-tested |
| R-32 | A note counts as sent when the Notecard accepts it, so a route that fails later (wrong URL, target down) is not seen by the device | Certain | Medium | Notehub keeps each event and shows route failures and retries; the User Manual tells the user to check the route log |
| R-33 | Notehub reaches the targets from the internet, so the LAN test servers (`192.168.2.37`, `192.168.2.231`) cannot be reached over cellular | Certain | Medium | Over cellular, ThingsBoard and ntfy must be the internet-reachable servers (`https://`, FR-32.9); the LAN servers stay for WiFi |
| R-35 | The Notecard's GPS turns on only after its first Notehub sync and never while its modem is on, a cold first fix takes 1 – 2 min or fails indoors, and the Notecard ends a seek after about 17 min (after 90 s with no signal at all), so a setup check may end without a fix | Likely | Low | The search runs for the whole hold (up to 30 min, FR-24.6) after the time step's session; the GPS antenna faces the sky; an earlier place stays stored; the next key wake or setup check tries again |
| R-36 | The pins of §35.2 come from Waveshare's V2 schematic; the camera's pins are free only with the CAM switch off, GPIO 45 (the modem's DTR) is a strapping pin the level shifter could hold at reset, and nearly every header pin is used | Possible | Medium | `commissioning/ws-probe` checks each pin and the modem on the board before wiring; a pin that does not work sends the map back to `/define` |
| R-37 | LTE Cat-1 has no power saving mode, so each send attaches afresh; the modem's supply follows GPIO 21 only with the board's 4G switch off (on SLOT2 the modem did not start with the switch on), and without a cell the modem never starts | Certain | Medium | FR-35.23 powers the modem down and cuts its supply after each send; the User Manual and the bench set the 4G switch off; NFR-35.3 measures the sleep current; `cell_plmn` locks a site to one network |
| R-38 | A microSD card in its slot may draw 100 µA or more in deep sleep, and this board does not switch the slot's supply | Possible | Medium | Commissioning measures the sleep current with and without a card; if it is high, the card is removed after setup, as the User Manual will say |
| R-39 | E-paper panels are rated for refreshes above about 0 °C; near 40 °F the panel is within its rating but slower | Likely | Low | FR-35.2 switches to the OLED below 40 °F with 2 °F hysteresis |
| R-40 | Notehub's routes offer JSONata only, so a `raw` message reaching Notehub could not be decoded there, and Notehub leaves zero fields out of a body | Certain | Medium | Notecard links use the template encoding (§37.2), which Notehub decodes; every decoder reads an absent field as 0 (§37.2, FR-37.4 contract) |
| R-41 | A gateway listens without sleeping, about 80 – 100 mA on an ESP32-S3 (an approximate figure, not yet measured) | Certain | Medium | A gateway is powered from USB or mains [user]; commissioning measures the draw (Phase 19) |
| R-42 | Range and airtime of ESP-NOW, and of its LR mode, on these boards' on-board antennas are only the vendor's figures (about 100 – 200 m normal, up to about 1 km LR) | Likely | Medium | `espnow_lr` stays `off` by default; the FR-38.7 range test records the delivered share at 100 m, 300 m and 1 km |
| R-43 | Unexpected Maker publishes no sleep current for the FeatherS3, so the build's gain over the T-Display build is not yet known | Certain | Medium | NFR-40.1 measures it part by part at commissioning; the limit is set from that figure [user] |
| R-44 | The pins of §40.2 beyond the board's named functions (GPIO 1, 5 – 7, 10 – 14, 17, 18, 33) come from distributor pages, not the board's schematic, and GPIO 33 may be taken by the PSRAM | Possible | Medium | The Phase 21 commissioning program walks each pin; a pin found taken moves before any firmware uses it |
| R-45 | No protector survives a direct strike, and a nearby one can still exceed C-40.7's ratings on a 100 ft cable | Possible | High | C-40.5 – C-40.8 limit the damage to the replaceable protector and field boards; the field boards plug in, so they are swapped, not repaired |
| R-46 | A 0 – 5 V probe has no live zero: a broken signal wire reads 0 V, the same as an empty tank | Certain | Medium | Accepted for the EALLT-1 [user]; a 4-20 mA probe on the loop channel detects it (FR-29.4) |
| R-48 | Satellite data costs by the byte, so a fault that adds messages (a flapping alert, a clock set wrong) could spend the month's data | Possible | Medium | FR-41.6 sends only critical alerts, capped a day, and FR-41.9 caps the month's bytes; the count is on `GET /` and the `day` flag `budget` |
| R-49 | Skylo coverage, the Notecard for Skylo's availability and Blues' NTN rules (templates, environment variables over NTN) are taken from Blues' documents, not yet from a run of this firmware | Possible | Medium | The sat-probe trial's stage 3 and the §41.6 operator rows prove them before the section is relied on |
| R-50 | §42's power, data and transfer times come from Blues' published figures for another duty cycle, and the I2C rate to the Notecard's binary buffer is unmeasured, so a year's history may take longer than NFR-42.1 allows | Possible | Medium | NFR-42.1 and NFR-42.2 are measured in the §42.4 operator rows; a slow transfer moves the history to a packed form, defined as a new requirement first |
| R-51 | Anyone who can send requests through ThingsBoard or Notehub can change a station's setup in a session | Possible | Medium | A session can change only FR-32.17's settings (FR-42.7, C-42.1), as a shared attribute can; ThingsBoard and Notehub accounts are the access control |
| R-47 | The EALLT-1's supply current is not published and may not fit the isolated 24 V beside a shorted loop | Possible | Medium | Measured at commissioning; C-40.10 sizes the converter from it |
| R-34 | The shunt's 0.1 % and the ADS1115's worst-case gain error of 0.15 % together can exceed NFR-29.1 at 20 mA (0.05 mA against 0.04 mA); typical parts are well inside it | Unlikely | Medium | The NFR-29.1 calibrator test decides on each board; a board that fails gets a one-point gain trim setting, defined as a new requirement first |
| R-27 | The testbench offers no RS232 peer today, so the bench tier of §30 needs a USB-RS232 adapter on the testbench host, wired to the DUT's DE-9, and a way for a test to drive it | Certain | Medium | The user adds the adapter (decided 2026-11-01); until the bench declares it, the RS232 bench rows are `not done` and the cores are host-tested; the loopback runs on the target tier |

### 4.2 Assumptions

- The board is powered from USB-C; no battery is fitted (assumed; not
  architecture-critical).
- The LAN the device joins is trusted (§9.1). [user]

### 4.3 Explicitly out of scope

- Authentication on any endpoint; TLS on anything the device serves (the summary email is sent over TLS, §25); signed firmware, secure boot, flash
  encryption (§9.1). [user]
- BLE outside a setup window (§26), BLE settings without a passkey (a bonded central
  passed one when it paired, FR-26.13); MQTT, IPv6, NTP, static IP, more than one stored network, a network scan
  list in the portal, a portal timeout. [user]
- OTA by pushed image body, HTTPS or authenticated downloads, OTA progress on
  the LCD, OTA of the bootloader or partition table. [user]
- mDNS service records (`_http._tcp`); only the hostname A record is answered.
- Recovering the display after a driver error; the display stays failed until
  reset (FR-5.27).
- Push notifications of routine readings or summaries; limits on add-on
  values; ntfy user-and-password sign-in, attachments, and servers with a
  self-signed certificate (§31). [user]
- The Notecard's own WiFi (its WiFi-first order with cellular fallback, `card.wifi`); a later option for sites with an unreliable access point. [user]
- Touch, GPIO 14 button, battery voltage, backlight dimming, PSRAM.
- Uptime and request counter wrap after 2³² − 1.
- Every ESP32-pack test family case not listed in a chapter here (curated subset,
  [user]).

### 4.4 Dependencies

- ESP-IDF **v6.0.2**, the image the Harness uses (`espressif/idf:v6.0.2`). [user]
- Managed component `espressif/mdns` (not bundled with IDF 6). [derived from §17]
- Blues Notehub (cloud) for the cellular uplink (§33), with the routes the user sets up; the Notecard's included data. [user]
- Embedded AI Harness testbench at `192.168.2.230`, SLOT1: USB flash, serial
  monitor, operator prompt, WiFi AP and station, HTTP relay, UDP log capture on
  port 5555, WiFi scan, port scan.

---

# Part A — Application Logic (L2)

## 5. Device Logic

### 5.1 Purpose

Decides which network role the device is in, when it retries, when it restarts,
whether a new image is confirmed, and what the LCD shows.

### 5.2 Data

| Item | Origin | Type / unit | Range | Notes |
|------|--------|-------------|-------|-------|
| Stored credentials | NVS (Appendix A) | SSID + password | see FR-13.4 | Present or absent at boot |
| WiFi switch | NVS (Appendix A) | `on` / `off` | §34.2 | `on` when none is stored |
| Retry count n | logic | unsigned | 0 – 2³² − 1 | Failed attempts since the last ONLINE; 0 at boot |
| Retry delay | derived from n | ms | FR-5.18 | |
| Image pending verify | bootloader / `otadata` | boolean | | True on the first boot of a newly uploaded image |
| Request count | §14 | unsigned 32-bit | 0 – 2³² − 1 | FR-14.6 |
| Screen lines 1 – 3 | derived from state | ASCII, ≤ 33 characters each | FR-5.29 | §5.4 |

### 5.3 State models

Two independent regions. The transition tables are normative; events not listed
for a state are logged at debug level and discarded.

#### Connectivity region

| State | Meaning |
|-------|---------|
| **BOOT** | From reset until credentials have been read |
| **PROVISIONING** | No credentials; SoftAP and portal (§13) active; station off |
| **CONNECTING** | A station connection attempt with the stored credentials is in progress |
| **ONLINE** | Station has an IPv4 address; status server, OTA, mDNS and UDP log active |
| **WIFI_LOST** | Between failed attempts, waiting for the retry delay |
| **WIFI_OFF** | `wifi` `off` stored (§34); station, SoftAP and portal off |

A connection attempt **fails** when the station reports a disconnect, or when it
has no IP address 15 s after the attempt started.

| # | Current | Event | Guard | Next | Action | Limit |
|---|---------|-------|-------|------|--------|-------|
| T1 | BOOT | credentials read | none stored, `wifi` not `off` | PROVISIONING | Start SoftAP, portal, captive DNS | `web: state=PROVISIONING` ≤ 3000 ms after reset |
| T2 | BOOT | credentials read | stored, `wifi` `on` | CONNECTING | Start station attempt; no SoftAP | `web: state=CONNECTING` ≤ 3000 ms after reset |
| T3 | PROVISIONING | credentials submitted (portal or BLE, FR-26.6) | valid (FR-13.4, FR-25.1) | CONNECTING | Store in NVS; answer 200; stop SoftAP; start station attempt | SoftAP gone ≤ 5 s after the answer |
| T4 | PROVISIONING | credentials submitted (portal or BLE) | invalid | PROVISIONING | Answer 400; NVS unchanged | — |
| T5 | CONNECTING | IP acquired | — | ONLINE | n := 0; start status server, mDNS, UDP log; if image pending verify, confirm it | — |
| T6 | CONNECTING | attempt failed | — | WIFI_LOST | n := n + 1; arm retry timer with delay(n) | — |
| T7 | ONLINE | disconnected | — | WIFI_LOST | Abort any OTA download (FR-15.13); stop status server, mDNS, UDP log; n := 1; arm retry timer with delay(1) | `web: state=WIFI_LOST` ≤ 15 s after the AP stops (NFR-5.3) |
| T8 | WIFI_LOST | retry timer | — | CONNECTING | Start station attempt | — |
| T9 | ONLINE | IP changed | — | ONLINE | Update the shown IP; log `web: online` again | — |
| T10 | PROVISIONING, CONNECTING, ONLINE, WIFI_LOST, WIFI_OFF | factory reset (§18) | — | restart → BOOT | Abort any OTA download; erase credentials; restart | restart ≤ 2 s after the event |
| T11 | ONLINE | OTA download succeeded (FR-15.10) | — | restart → BOOT | Select new slot; restart | restart ≤ 2 s after `OTA succeeded` |
| T12 | ONLINE | OTA download failed (FR-15.11) | — | ONLINE | Boot slot unchanged | — |
| T13 | BOOT, PROVISIONING, CONNECTING, WIFI_LOST | 60 s since reset | image pending verify | restart → BOOT | Log; restart without confirming (bootloader rolls back) | restart ≤ 2 s after the deadline |
| T14 | CONNECTING, ONLINE, WIFI_LOST, WIFI_OFF | BLE settings accepted (key window, FR-26.7) | — | restart → BOOT | Store the settings; notify `result`; close the window; restart | restart ≤ 2 s after the notification (FR-26.8) |
| T15 | BOOT | credentials read | `wifi` `off` stored | WIFI_OFF | No station, no SoftAP (FR-34.4) | `web: state=WIFI_OFF` ≤ 3000 ms after reset |
| T16 | PROVISIONING | credentials submitted (portal or BLE) | valid, `wifi` `off` (FR-34.2) | WIFI_OFF | Store in NVS; answer 200 or `ok`; stop SoftAP and the provisioning window; no station (FR-34.5) | SoftAP gone ≤ 5 s after the answer |

**Completeness.** Events: credentials read (CR), credentials submitted (CS), IP
acquired (IP), attempt failed (AF), disconnected (DC), retry timer (RT), IP
changed (IC), factory reset (FR), OTA succeeded (OS), OTA failed (OF), 60 s since
reset (60), BLE settings accepted (BS). A BLE `settings` write in PROVISIONING is
CS, not BS.
- Any state × BS other than CONNECTING, ONLINE, WIFI_LOST, WIFI_OFF: impossible — a key
  window exists only in those states (FR-26.2), and a BLE write in PROVISIONING
  is CS.
- BOOT × CS / IP / AF / DC / RT / IC / OS / OF: impossible — no network interface
  is started in BOOT. × FR: impossible — the button is not sampled until BOOT
  exits. × 60 without image pending verify: ignored.
- PROVISIONING × CR: impossible — credentials are read once, in BOOT. × IP / AF /
  DC / RT / IC: impossible — the station is off. × OS / OF: impossible — the
  portal has no `/ota` route (FR-13.6 answers it with a redirect). × 60 without
  image pending verify: ignored.
- CONNECTING × DC: an attempt failed (§5.3 definition) — T6. × CR: impossible
  (read once, in BOOT). × CS: impossible (no portal).
  × IC: impossible (no address yet; the first address is IP). × OS / OF:
  impossible — a download exists only in ONLINE and T7 aborts it on leaving. × RT: impossible (timer armed only in
  WIFI_LOST). × 60 without image pending verify: ignored.
- ONLINE × CR: impossible. × CS: impossible. × IP: impossible — the station
  already has an address; a new one arrives as IC. × AF / RT: impossible (no
  attempt in progress, timer cancelled on T5). × 60: ignored — an image pending
  verify was confirmed by T5, so the guard is false.
- WIFI_LOST × CR / CS: impossible. × OS / OF: impossible — T7 aborted any
  download (logging K11 as part of T7) before this state was entered. × IP / IC: impossible (no attempt in progress). × AF:
  impossible (no attempt in progress). × DC: ignored. × 60 without image pending
  verify: ignored.
- WIFI_OFF × CR: impossible (read once, in BOOT). × CS: impossible (no portal;
  a BLE write here is BS). × IP / AF / DC / RT / IC: impossible — the station
  is off. × OS / OF: impossible — no download is ever started (FR-34.8).
  × 60 with image pending verify: impossible — only an OTA download makes an
  image pending, OTA needs ONLINE, and FR-26.17 keeps BS from changing
  `wifi` during a download; without it: ignored.
- Any state × reset or watchdog reset (§19): restart into BOOT; only credentials
  and the other settings persist.
- **Prohibited:** while credentials or `wifi` `off` are stored, any transition into
  PROVISIONING (only T10's erase and restart lead back to it through T1,
  FR-5.19, FR-34.6); any restart other than T10, T11, T13, T14 and the watchdog.

#### Display region

| State | Meaning |
|-------|---------|
| **LCD_START** | From reset until the first LCD start attempt completes |
| **LCD_OK** | The display driver has reported no error |
| **LCD_FAILED** | The display driver has reported an error; no further LCD call until reset |

| # | Current | Event | Guard | Next | Action | Limit |
|---|---------|-------|-------|------|--------|-------|
| D1 | LCD_START | start done | ok | LCD_OK | Clear to black; draw the BOOT screen; backlight on | 3000 ms after reset |
| D2 | LCD_START | start done | failed | LCD_FAILED | Log `web: lcd init failed: <ERR>` | 3000 ms after reset |
| D3 | LCD_OK | 1 s tick | screen unchanged | LCD_OK | none | 1000 ± 100 ms period |
| D4 | LCD_OK | 1 s tick | changed, write ok | LCD_OK | Redraw each changed line; log FR-7.34 for each | 1000 ± 100 ms period |
| D5 | LCD_OK | 1 s tick | changed, write failed | LCD_FAILED | Log `web: lcd write failed: <ERR>` | same tick |
| D6 | LCD_FAILED | 1 s tick | — | LCD_FAILED | No LCD call | — |

Completeness: LCD_START × tick: impossible, the tick starts on exit from
LCD_START. LCD_OK × start done and LCD_FAILED × start done: impossible — the LCD is
started exactly once, in LCD_START. Connectivity events never change the display
region; they change the screen content that D3 – D5 compare against.

### 5.4 Screen content

Six text lines, landscape: lines 1 – 5 at most 33 characters each, line 6
in a smaller font at most 44 (FR-6.14, FR-6.15). Line 4 is the
reading in every state (FR-23.12); line 5 is empty except during the setup
check (FR-24.13) and a BLE window (FR-26.23); line 6 holds the add-on values
(FR-29.8, FR-30.10), empty with every add-on off, except that it shows the
GPS search during a setup check with `gps` `on` (FR-33.19); the table gives lines 1 – 3.

| Connectivity state | Line 1 | Line 2 | Line 3 |
|--------------------|--------|--------|--------|
| BOOT | `Starting` | (empty) | (empty) |
| PROVISIONING | `Setup WiFi` | the SoftAP SSID (FR-13.1) | `192.168.4.1` |
| CONNECTING | `Connecting` | the stored SSID, per FR-5.29 | (empty) |
| ONLINE | `IP <a.b.c.d>` | `Req: <n>` | `<hostname>.local` (FR-20.1) |
| WIFI_LOST | `WiFi lost` | `Retrying` | (empty) |
| WIFI_OFF | `WiFi off` | `Hold key 3 s for BLE` | (empty) |

`<n>` is the request count of FR-14.6 in decimal, no leading zeros.

### 5.5 Requirements

Connectivity:

- **FR-5.11** [Must] [user] When no credentials are stored at boot, the device
  shall enter PROVISIONING (T1), unless `wifi` `off` is stored (FR-34.4).
- **FR-5.12** [Must] [user] When credentials are stored at boot, the device shall
  enter CONNECTING (T2) without starting the SoftAP, unless `wifi` `off` is
  stored (FR-34.4).
- **FR-5.13** [Must] [user] When valid credentials are submitted in PROVISIONING,
  the device shall store them in NVS and enter CONNECTING without restarting (T3).
- **FR-5.14** [Must] [derived from FR-13.4] When invalid credentials are submitted
  in PROVISIONING, the device shall remain in PROVISIONING with the NVS contents
  unchanged (T4).
- **FR-5.15** [Must] [user] When the station acquires an IPv4 address in
  CONNECTING, the device shall enter ONLINE (T5).
- **FR-5.16** [Must] [user] When a connection attempt fails, the device shall
  enter WIFI_LOST and arm a retry (T6).
- **FR-5.30** [Must] [user] When the station disconnects in ONLINE, the device
  shall enter WIFI_LOST and arm a retry (T7).
- **FR-5.17** [Must] [user] When the retry timer expires in WIFI_LOST, the device
  shall start a new connection attempt with the stored credentials (T8).
- **FR-5.18** [Must] [pack:esp32] The retry delay after the n-th consecutive failed
  attempt shall be min(2ⁿ⁻¹, 15) s (1, 2, 4, 8, 15, 15, … s), with a
  tolerance of ± 10 %.
- **FR-5.19** [Must] [user] While credentials are stored, the device shall not
  enter PROVISIONING (only T10's erase leads back to it).
- **FR-5.20** [Must] [user] Stored credentials shall survive a reset and a power
  cycle.
- **FR-5.21** [Must] [pack:esp32] On a factory-reset event (§18), the device shall
  erase the stored credentials and restart within 2 s (T10).
- **FR-5.32** [Must] [user] When an OTA download succeeds (FR-15.10), the device
  shall restart into the new image within 2 s of logging `OTA succeeded` (T11).
- **FR-5.23** [Must] [user] An image booted pending verification shall mark itself
  valid on entering ONLINE (T5).
- **FR-5.24** [Must] [user] An image booted pending verification that has not
  entered ONLINE within 60 s of reset shall restart without confirming,
  so that the bootloader boots the previous image (T13).
- **NFR-5.3** [Must] [user] When the AP stops while the device is ONLINE, the
  device shall log `web: state=WIFI_LOST` within 15 s of the AP stopping.
- **NFR-5.1** [Must] [user] With credentials stored and the AP available, the
  device shall log `web: online` within 10 s of reset, by its own log
  timestamp.
- **NFR-5.2** [Must] [user] After the AP returns from an outage of up to 60 s, the
  device shall log `web: online` within 30 s of the AP becoming
  available, without restarting.

Display:

- **FR-5.25** [Must] [user] In LCD_OK, the screen shall show the lines of §5.4
  for the current connectivity state.
- **FR-5.26** [Must] [user] In LCD_OK, a change of screen content shall be shown
  within 2 s of the change (one 1 s refresh plus tolerance).
- **FR-5.27** [Must] [derived, carried from base FR-5.6/5.8/5.9] After an LCD start
  or write error, the device shall make no further LCD call until the next reset
  (D2, D5, D6).
- **FR-5.28** [Must] [derived, carried from base FR-5.10] An LCD error shall cause
  neither a restart nor any change in the connectivity region.
- **FR-5.31** [Must] [user] In no state shall the screen show `Hello, world!` or a
  line beginning `Up:`.
- **FR-5.29** [Must] [derived from FR-6.15's glyph range] A displayed SSID shall
  render each byte outside 0x20 – 0x7E as `?` and be cut to its first 20
  characters.

### 5.6 Verification contracts

Test builds named here (how they are built is HOW):
- **LCD fault build** — the shipped firmware with an LCD that fails at start or at
  the k-th write (exists: `tests/target/lcd_fault/`).
- **No-network build** — a valid image, with an embedded version different from
  every shipped build's, identical to the shipped one except that it
  never starts a station attempt: with stored credentials it stays in CONNECTING
  (T2), so it never reaches ONLINE and never confirms — the failure FR-5.24 exists
  for.

Compact form:

| ID | Precondition · stimulus | Expected observation | Must NOT happen | Timing | Tier |
|----|------------------------|----------------------|-----------------|--------|------|
| FR-5.11 | Flash erased then shipped build flashed; USB reset | `web: state=PROVISIONING`; bench scan sees the FR-13.1 SSID | `state=CONNECTING`; no SoftAP | ≤ 3000 ms log timestamp | bench |
| FR-5.11 (core) | Transition function: BOOT, credentials read, none | Next = PROVISIONING; action starts SoftAP | Station start | n/a | host |
| FR-5.12 | Provisioned; USB reset | `web: state=CONNECTING`; bench scan shows no FR-13.1 SSID for 30 s | `state=PROVISIONING`; SoftAP seen | ≤ 3000 ms log timestamp | bench |
| FR-5.13 | PROVISIONING; bench `enter-portal` submits valid credentials | `web: provisioned ssid=<s>`, then `web: state=CONNECTING`; no boot line between | A boot line (restart); `state=PROVISIONING` again | SoftAP gone from scan ≤ 5 s after the 200 | bench |
| FR-5.14 | PROVISIONING; bench joins SoftAP, POSTs a 33-byte SSID | 400; device still PROVISIONING; a following USB reset enters PROVISIONING again | 200; NVS write (CONNECTING after reset) | n/a | bench |
| FR-5.14 (core) | Transition function: PROVISIONING, submitted, invalid | Next = PROVISIONING; no store action | Store action | n/a | host |
| FR-5.15 | CONNECTING, bench AP up | `web: state=ONLINE` and `web: online ip=<a.b.c.d>` (either order) in the same boot, the IP equal to the bench AP's station entry | `web: state=ONLINE` with no IP line; a state other than ONLINE | n/a | bench |
| FR-5.16, FR-5.17, FR-5.30 (core) | Transition function: CONNECTING failed; ONLINE disconnected; WIFI_LOST retry | T6, T7, T8 as tabled | Any other next state; PROVISIONING | n/a | host |
| FR-5.18 (core) | Delay function n = 1 … 8 | 1, 2, 4, 8, 15, 15, 15, 15 s | Any other value; unbounded growth | n/a | host |
| FR-5.18 | ONLINE; bench AP stopped for 90 s | L6 lines announce 1, 2, 4, 8, 15, 15 … s; each following `web: state=CONNECTING` comes that delay after its L6 line, by log timestamps | A gap > 16.5 s; retries stop; a delay differing from its L6 line by > 10 % | ± 10 % | bench |
| FR-5.32 | ONLINE; request a valid hosted image (FR-15.10) | `OTA succeeded`, then a boot line with the new `fw=` | No restart; old `fw=` after the restart | restart ≤ 2 s after `OTA succeeded` | bench |
| NFR-5.3 | ONLINE; bench AP stopped | `web: state=WIFI_LOST` | No state change; a boot line; PROVISIONING | ≤ 15 s after the AP stop, by the bench activity log and the serial capture; up to + 3 s late (Appendix A), never before the stop | bench |
| FR-5.23 | After FR-5.32 | `web: ota confirmed` once ONLINE; a further USB reset boots the same `fw=` | Rollback on the next reset | n/a | bench |
| FR-5.25 | Each connectivity state reached on the bench; operator prompted per state | Operator confirms the §5.4 lines; the FR-7.34 mirror lines match them | Missing, stale or extra text | n/a | bench (operator) |
| FR-5.25 (core) | Screen function for every state, with sample SSID/IP/n/hostname | The §5.4 strings exactly | Any other string | n/a | host |
| FR-5.26 | ONLINE; one `GET /api/status` | FR-7.34 line `line2="Req: <n>"` with the new n | No mirror line | ≤ 2 s after the response | bench |
| FR-5.27 | LCD fault build failing at start; USB reset; 30 s capture | Exactly one `lcdfi: call` before `web: lcd init failed:` | Any `lcdfi: call` after it | 30 s window | target |
| FR-5.31 (core) | Screen function for every §5.3 state and every display-region state | No line equals `Hello, world!`; no line starts `Up:` | Either text in any state | n/a | host |
| FR-5.31 | Shipped build, JRN-01 run from erase to ONLINE with 3 requests; operator prompted at PROVISIONING and ONLINE | No FR-7.34 mirror line with either text; operator confirms neither is on the panel | `Hello, world!` or `Up:` on the panel or in a mirror line | n/a | bench (operator) |
| FR-5.29 (core) | SSIDs: 32 × `A`; `Caf\xc3\xa9`; empty | `AAAAAAAAAAAAAAAAAAAA`; `Caf??`; empty line | Wider than 20; raw non-ASCII byte | n/a | host |
| NFR-5.1 | Provisioned; bench AP up; USB reset; 5 runs | `web: online` log timestamp ≤ 10000 ms in every run | A run over 10000 ms; PROVISIONING | ≤ 10 s | bench |

Full form (a wrong pass is plausible):

```yaml
id: NFR-5.2
verification:
  preconditions:
    - Device provisioned for the bench AP and ONLINE.
  stimulus:
    - Stop the bench AP; keep it stopped for 60 s; start it again with the same SSID and password.
  expected_observations:
    - "web: state=WIFI_LOST" within 15 s (+ 3 s, Appendix A) of the AP stopping (NFR-5.3).
    - "web: retry in" lines during the outage.
    - "web: online ip=" after the AP restarts.
  timing: "web: online" within 30 s of the AP start, by bench wall clock of the UDP or serial capture
  tolerance: + 3 s late (Appendix A), never early
  prohibited_outcomes:
    - A boot line ("web: boot reason=") anywhere in the window — recovery by restart.
    - "web: state=PROVISIONING" or the device SoftAP seen in a scan.
    - Retries stopping before the AP returns.
  tier: bench
  evidence:
    - AP stop and start timestamps from the bench activity log.
    - Serial capture of the whole window.
  cleanup:
    - Bench AP running with the test credentials; device ONLINE.
```

```yaml
id: FR-5.19
verification:
  preconditions:
    - Device provisioned for the bench AP; bench AP stopped.
  stimulus:
    - USB reset with the AP still stopped; observe 120 s; then start the AP.
  expected_observations:
    - "web: state=CONNECTING" and "web: state=WIFI_LOST" alternate for 120 s.
    - After the AP starts, "web: online".
  timing: 120 s observation window
  tolerance: none
  prohibited_outcomes:
    - "web: state=PROVISIONING" at any point.
    - The FR-13.1 SSID in any bench scan during the window.
    - Credentials erased (device not online after the AP returns).
  tier: bench
  evidence:
    - Serial capture of the window; bench WiFi scans at 30 s intervals.
  cleanup:
    - Bench AP running; device ONLINE.
```

```yaml
id: FR-5.20
verification:
  preconditions:
    - Device provisioned and ONLINE.
  stimulus:
    - USB reset; wait for ONLINE. Then an operator unplugs and replugs SLOT1's USB cable (power cycle).
  expected_observations:
    - After each restart, "web: state=CONNECTING" then "web: online" with no portal.
  timing: "web: online" within NFR-5.1's deadline after each restart
  tolerance: none
  prohibited_outcomes:
    - "web: state=PROVISIONING" after either restart.
    - The bench having to submit credentials again.
  tier: bench (operator for the power cycle)
  evidence:
    - Serial capture of both boots; operator confirmation of the power cycle.
  cleanup:
    - Device ONLINE.
```

```yaml
id: FR-5.21
verification:
  preconditions:
    - Device provisioned and ONLINE; operator at the bench.
  stimulus:
    - Operator holds the BOOT button for about 7 s, then releases it (P-BUTTON).
  expected_observations:
    - L17b ("web: button held, release to reset") about 5 s into the hold.
    - After the release, "web: factory reset", then a boot line, then "web: state=PROVISIONING".
    - With the bench AP stopped (one bench radio cannot scan while it beacons), the FR-13.1 SSID appears in a bench scan.
    - A further USB reset enters PROVISIONING again (credentials are gone, not merely ignored).
  timing: boot line within 2 s of "web: factory reset" by log order and bench capture time
  tolerance: none
  prohibited_outcomes:
    - "web: state=CONNECTING" after the restart.
    - No restart (PROVISIONING reached without the boot line is not this behaviour).
    - A reset while BOOT was still held (ROM "waiting for download" banner, no boot line).
  tier: bench (operator)
  evidence:
    - Operator prompt answer with timestamps; serial capture; bench scan result.
  cleanup:
    - Re-provision through the bench enter-portal procedure.
```

```yaml
id: FR-5.24
verification:
  preconditions:
    - Shipped build A running, confirmed and ONLINE; no-network build B available (credentials stay stored; the bench AP stays up).
  stimulus:
    - Host build B on the bench (/api/firmware/upload) and POST /ota with its URL.
  expected_observations:
    - A boot line with fw=B.
    - B logs "web: ota not confirmed, restarting" (L16), then a restart and a boot line with fw=A.
    - "web: rolled back, invalid=<slot of B>" in A's boot.
  timing: L16 at B's log timestamp 60 s – 63 s after B's reset (60 s deadline, up to + 3 s late, Appendix A)
  tolerance: + 3 s, never early
  prohibited_outcomes:
    - L16 before 60 s (restart too early), or B still running after 63 s + 2 s.
    - A restart without L16 (a crash, not the confirm deadline).
    - A third boot with fw=B (B retried).
    - Credentials lost (A enters PROVISIONING).
  tier: bench
  evidence:
    - Serial capture from the upload to A's "web: online".
  cleanup:
    - Device running the shipped build, confirmed.
```

```yaml
id: FR-5.28
verification:
  preconditions:
    - LCD fault build; run 1 failing at start, run 2 failing at the 5th write. Device provisioned.
  stimulus:
    - With the bench AP stopped, reset through JTAG (P-RESET: no extra boot); start the bench AP; after the LCD failure line, request GET /api/status every 5 s for 60 s.
  expected_observations:
    - "web: online" in both runs.
    - Every request answered 200 with requests increasing by 1.
  timing: 60 s window per run
  tolerance: none
  prohibited_outcomes:
    - A boot line after the first one in either run.
    - "web: state=" change other than those the network caused.
  tier: bench
  evidence:
    - Serial capture and relay responses of both runs.
  cleanup:
    - Reflash the shipped build.
```

---

# Part B — Interfaces (L1)

## 6. LCD Display Interface

### 6.1 Purpose & peer

Powers the built-in RM67162 AMOLED panel, sets its brightness, renders six text
lines in landscape (two fonts, FR-6.15), and reports whether each driver call succeeded. Direction:
device → display.

### 6.2 Protocol

ESP-IDF SPI master bus in quad mode through esp_lcd's SPI panel IO, at the clock
of C-6.4, with the pins of C-6.1 and C-6.2. Registers are written with opcode 0x02
on one line, pixels with opcode 0x32 and the data on four lines. "Display failed"
means a bus, panel IO or register/pixel write call returns a value other than
`ESP_OK`. The bus is write-only, so a physically absent or broken panel is not
detected (R-2).

### 6.3 Requirements

- **C-6.1** [Must] [derived from the LilyGO T-Display-S3 AMOLED pin map] The QSPI
  data lines D0 – D3 shall be GPIO 18, 7, 48, 5 in that order.
- **C-6.2** [Must] [derived from the LilyGO T-Display-S3 AMOLED pin map] The LCD
  control lines shall be SCK = GPIO 47, CS = GPIO 6 and RST = GPIO 17.
- **C-6.3** [Must] [derived from the LilyGO T-Display-S3 AMOLED pin map] GPIO 38
  (LCD power enable) shall be driven high before panel initialisation begins.
- **C-6.4** [Must] [user] The QSPI clock shall be requested at 75 MHz.
- **C-6.5** [Must] [derived from commissioning] The panel shall be addressed in
  landscape with MADCTL 0x60 and no controller-RAM offset.
- **FR-6.1** [Must] [user] In LCD_OK, text shall be upright and read left to right
  when the board is held in landscape with its USB-C connector to the viewer's
  right.
- **FR-6.15** [Must] [user] Text shall be rendered in fixed-width fonts
  covering ASCII 0x20 – 0x7E: a 16 × 32 pixel cell on lines 1 – 5 and a
  12 × 24 pixel cell (Terminus Bold, generated by `bdf_to_c.py`) on line 6.
- **FR-6.3** [Must] [user] In LCD_OK, text shall be white on a black background.
- **FR-6.4** [Must] [derived from FR-5.25] On entry to LCD_OK (D1), the full
  536 × 240 visible area shall be cleared to black, leaving no lit pixels outside
  the text lines.
- **FR-6.14** [Must] [user] Lines 1 – 6 shall not overlap and shall lie
  entirely inside the visible area: tops at 8, 48, 88, 128, 168 and 208 px,
  8 px apart. A line of 44 characters on line 6 shall be
  displayed in full (44 × 12 px = 528 px of the 536).
- **FR-6.13** [Must] [user] A line of 33 characters shall be displayed in full,
  without clipping or wrapping (33 × 16 px = 528 px of the 536).
- **FR-6.7** [Must] [derived from FR-5.25] In LCD_OK, the panel brightness
  (register 0x51) shall be set non-zero; before LCD_OK it stays 0.
- **FR-6.8** [Must] [derived from FR-5.26] Redrawing a line shall overwrite its
  whole row, leaving no characters of its previous, longer text.

### 6.4 Verification contracts

| ID | Precondition · stimulus | Expected observation | Must NOT happen | Tier |
|----|------------------------|----------------------|-----------------|------|
| C-6.1 – C-6.5 | Inspect the built configuration | The pin numbers, 75 MHz clock and MADCTL 0x60 appear in the firmware's own configuration | Any other value; a value taken from a driver default | host |
| C-6.1 – C-6.5 (wire) | Shipped build; reset; operator prompted after the BOOT screen | Operator confirms `Starting` is legible | Blank, noisy, shifted or garbled panel | bench (operator) |
| FR-6.1 | LCD_OK; operator prompted with the board held USB-C to the right | Operator confirms text upright and left to right | Mirrored, upside-down or rotated text | bench (operator) |
| FR-6.15 | Inspect the fonts used by the renderer | Cells 16 × 32 px (lines 1 – 5) and 12 × 24 px (line 6); glyphs 0x20 – 0x7E | Other cell size; proportional font | host |
| FR-6.3 | LCD_OK; operator prompted | Operator confirms white on black | Inverted or tinted colours | bench (operator) |
| FR-6.4 | Power-cycle (random panel RAM); operator prompted after the first screen | No stray pixels along any edge or between lines | Noise band along an edge; remnants | bench (operator) |
| FR-6.14 | Render six lines with the shipped layout, line 6 with 44 characters | Each line's top row > the previous line's bottom row; all glyphs inside x ∈ [0, 535], y ∈ [0, 239]; line 6 one row, rightmost pixel x ≤ 527 | Overlap; a pixel outside; wrap | host |
| FR-6.13 | Render a 33-character line | Rightmost pixel x ≤ 527; one row | Width > 528 px; wrap | host |
| FR-6.7 | LCD_OK; operator prompted | Panel lit | Dark or flickering | bench (operator) |
| FR-6.8 | Render `Req: 1000`, then `Req: 5` into the same row | Row after the second render equals a fresh render of `Req: 5` | Remnant pixels of `000` | host |

### 6.5 Failure modes

Any esp_lcd call that does not return `ESP_OK` → the interface reports "display
failed" with the `esp_err_t` value to §5; it never retries on its own and never
aborts. What happens next is §5's decision (D2, D5).

---

## 7. Serial Log Interface

### 7.1 Purpose & peer

The machine-readable record of every decision, read by the testbench serial
monitor on SLOT1 and, in ONLINE, mirrored over UDP (§16). Direction: device →
bench. Transport: ESP-IDF log over USB-Serial/JTAG (C-10.5). Tag: `web`.

### 7.2 Line formats

`<ERR>` is the name `esp_err_to_name()` returns. `<R>` is the reset reason's
`esp_reset_reason_t` enumerator name without the `ESP_RST_` prefix (e.g.
`POWERON`, `SW`, `TASK_WDT`, `USB`). `<V>` is the running image's version
(FR-20.3). `<S>` is a §5.3 connectivity state name. `<k>` is 1, 2 or 3.

| # | Event | Line (after the ESP-IDF prefix `<L> (<ms>) `) | Level |
|---|-------|-----------------------------------------------|-------|
| L1 | Every boot | `web: boot reason=<R> fw=<V> slot=<running partition label>` | I |
| L2 | Entering a connectivity state | `web: state=<S>` | I |
| L3 | Entering PROVISIONING | `web: portal ssid=<SoftAP SSID> ip=192.168.4.1` | I |
| L4 | T3 | `web: provisioned ssid=<SSID>` | I |
| L5 | Entering ONLINE, and T9 | `web: online ip=<a.b.c.d> rssi=<dBm>` | I |
| L6 | T6, T7 | `web: retry in <ms> ms attempt=<n> reason=<r>` | W |
| L7 | D4 | `web: lcd line<k>="<text>"` | I |
| L8 | D2 | `web: lcd init failed: <ERR>` | E |
| L9 | D5 | `web: lcd write failed: <ERR>` | E |
| L10 | Status-server answer | `web: http <METHOD> <path> <status> n=<count>` | I |
| L11 | OTA download succeeded | `web: ota downloaded bytes=<n>` | I |
| L12 | OTA request refused (400, 409) | `web: ota refused <status>: <reason>` | W |
| L13 | T5 with image pending verify | `web: ota confirmed` | I |
| L14 | Every boot while the bootloader records an invalid (rolled-back) slot | `web: rolled back, invalid=<partition label>` | W |
| L15 | T10 | `web: factory reset` | W |
| L17b | BOOT button held 5 s (FR-18.3 threshold reached) | `web: button held, release to reset` | W |
| L16 | T13 | `web: ota not confirmed, restarting` | E |
| L18 | Entering deep sleep (FR-21.8) | `web: sleep s=<N>` | I |
| L19 | Boot that is a deep-sleep wake (FR-21.8) | `web: wake cause=<timer\|button>` | I |
| L20 | Clock synced (FR-22.5, FR-33.12, FR-39.2, FR-39.5) | `web: time synced <YYYY-MM-DD>T<hh:mm:ss><±hhmm>[ src=<cell\|rtc\|phone>]` (local time; `src=cell` when set from the Notecard or the modem, `rtc` from the hardware clock, `phone` from the BLE page) | I |
| L21 | Reading taken (FR-23.5) | `web: reading t=<°F, 2 decimals> rh=<%, 1 decimal> p=<mbar, 2 decimals> at=<YYYY-MM-DD>T<hh:mm:ss><±hhmm>, or none (FR-22.6)> synced=<0\|1>` | I |
| L22 | Sensor failed (FR-23.4) | `web: bme280 failed: <ERR>` | E |
| L23 | Setup check hold entered (FR-24.8) | `web: setup check pending` | I |
| L24 | Setup check hold ended (FR-24.11, FR-24.6) | `web: setup check done by=<key\|timeout>` | I |
| L25 | Summary sent (FR-25.9) | `web: mail sent to=<mail_to> readings=<n>` | I |
| L26 | Summary email dropped (FR-25.14) | `web: mail failed: <wifi\|dns\|connect\|tls\|cert\|auth\|refused\|timeout> to=<address>` | W |
| L27 | BLE window opened (FR-26.16) | `web: ble on reason=<provisioning\|key>` | I |
| L28 | BLE pairing ended (FR-26.16) | `web: ble pairing <ok\|failed>` | I (W when failed) |
| L29 | BLE `settings` written (FR-26.16) | `web: ble settings <ok\|invalid=<field>>` | I (W when invalid) |
| L30 | BLE window closed (FR-26.16) | `web: ble off reason=<provisioned\|idle\|done\|written\|pairing>` | I |
| L31 | History unavailable or failed (FR-27.8, FR-27.9) | `web: history unavailable` or `web: history failed: <ERR>` | W |
| L32 | Test started (FR-28.12) | `web: test start id=<quick\|cycle\|gps>` | I |
| L33 | Test ended (FR-28.12) | `web: test end id=<quick\|cycle> readings=<n>/<m> averages=<a>/<b> result=<pass\|fail>[ by=<key\|reset>]`; for `gps` (FR-33.20) `web: test end id=gps fix=<yes\|no> result=<pass\|fail>[ by=<key\|reset>]` | I (W when failed) |
| L34 | Loop value (FR-29.5) | `web: loop ma=<mA, 3 decimals> v=<value, 3 decimals> <loop_unit>` | I |
| L35 | Loop failed (FR-29.4) | `web: loop failed: <fault\|ERR> ma=<mA, 3 decimals, or none>` | W |
| L36 | History erased for its new format (FR-29.12) | `web: history erased reason=format` | W |
| L37 | RS232 value (FR-30.7) | `web: rs232 <rs_name>=<value, rs_dec decimals>[ <rs_unit>]` | I |
| L38 | RS232 failed (FR-30.6) | `web: rs232 failed: <timeout\|long\|nonum\|ERR>[ line=<first 32 bytes>]` | W |
| L39 | Alert delivered (FR-31.12) | `web: ntfy sent <event>` (§31.3 names) | I |
| L40 | Alert not delivered (FR-31.11) | `web: ntfy failed: <wifi\|dns\|connect\|tls\|cert\|auth\|refused\|limit\|server\|timeout> <event>` | W |
| L41 | ThingsBoard request accepted (FR-32.13) | `web: tb sent what=<telemetry\|attributes> n=<readings> left=<readings still after the mark>` | I |
| L42 | ThingsBoard request failed (FR-32.11) | `web: tb failed: <wifi\|dns\|connect\|tls\|cert\|auth\|refused\|server\|timeout> what=<telemetry\|attributes> n=<readings>` | W |
| L43 | Note accepted by the Notecard (FR-33.11) | `web: cell note <tb\|tbattr\|alert\|mail> n=<readings, or 1>` | I |
| L44 | Notecard request failed (FR-33.4, FR-33.10) | `web: cell failed: <timeout\|err\|full> <req>` | W |
| L45 | GPS search ended in the setup check (FR-33.17, FR-33.18) | `web: gps fix lat=<±d.ddddd> lon=<±d.ddddd>` or `web: gps none` | I |
| L46 | Modem attached (FR-35.23) | `web: modem attach ms=<n> net=<MCC+MNC> csq=<n>` | I |
| L47 | Modem failed (FR-35.9) | `web: modem failed: <power\|sim\|register\|ppp> ms=<n>` | W |
| L48 | Display chosen for the window (FR-35.2) | `web: display <epaper\|oled> t=<°F, 2 decimals, or none>` | I |
| L49 | Settings card (FR-35.4, FR-35.19, FR-35.20) | `web: card settings <ok\|invalid=<field>\|none\|failed>` | I |
| L55 | Hardware clock checked at a network sync (FR-39.8) | `web: rtc drift ms=<±n> days=<d.d> trim=<±n>[ was=<±n>]` (`was` when the trim changed; trim in 0.2384 ppm steps) | I |
| L56 | Hardware clock lost or failing (FR-39.9) | `web: rtc lost` or `web: rtc failed: <esp_err name>` | W |
| L50 | Displays found for the window (FR-35.25) | `web: displays epaper=<found\|none\|off> oled=<found\|none\|off>` | I |
| L57 | ESP-NOW frame delivered (FR-38.11) | `web: espnow sent what=<rd\|al\|st> n=<readings, or 1> ch=<c> lr=<0\|1> tries=<t> left=<readings still after the mark>` | I |
| L58 | ESP-NOW frame not delivered (FR-38.11) or channel search failed (FR-38.6) | `web: espnow failed: <noreply\|search> what=<rd\|al\|st\|search> ch=<c>` | W |
| L59 | Gateway accepted a frame (FR-38.16) | `web: espnow rx node=<hostname> what=<rd\|al\|st> n=<readings, or 1> rssi=<dBm>` | I |
| L60 | Gateway dropped a frame (FR-38.16) | `web: espnow dropped: <tag\|replay\|version\|full> mac=<aa:bb:cc:dd:ee:ff>` | W |
| L61 | Gateway passed a node's data on (FR-38.17, FR-38.18) | `web: espnow fwd node=<hostname> to=<tb\|ntfy> <ok\|failed: <connect\|tls\|auth\|refused\|timeout>> n=<readings, or 1>` | I or W |
| L62 | Level probe value (FR-40.11) | `web: level volt=<V, 3 decimals> v=<value, 3 decimals> <loop_unit>` | I |
| L63 | Level probe failed (FR-40.10) | `web: level failed: <over\|ERR> volt=<V, 3 decimals, or none>` | W |
| L64 | Remote settings received (FR-32.19) | `web: tb remote applied=<keys, comma-separated, or none>` | I |
| L66 | Satellite remote settings applied (FR-41.11) | `web: sat settings applied=<variables, comma-separated>` | I |
| L67 | Satellite send or setting withheld (FR-41.2, FR-41.6, FR-41.9, FR-41.11) | `web: sat skipped: <nomodem\|alerts\|budget\|invalid=<variable>>[ used=<bytes this month>]` | W |
| L68 | Remote session scheduled, opened or ended (FR-42.1, FR-42.4, FR-42.9) | `web: session <scheduled <start>\|open until <end>\|ended <end\|request\|battery>>` | I |
| L69 | Remote session refused or failed (FR-42.1, FR-42.5) | `web: session failed: <invalid\|nocell>` | W |
| L65 | Remote settings refused or failed (FR-32.19, FR-32.21) | `web: tb remote failed: <invalid=<key>\|connect\|tls\|auth\|refused\|timeout>` | W |
| L17 | End of startup | `Init complete, running event-driven` (any tag) | I |

### 7.3 Requirements

Each line requirement is one row of §7.2:

- **FR-7.5** [Must] [derived] Every line in §7.2 shall carry the ESP-IDF log prefix
  with a millisecond-since-boot timestamp.
- **FR-7.6** [Must] [user, testbench compatibility contract] Once startup has
  finished, the device shall log exactly `Init complete, running event-driven`,
  once per boot (L17).
- **FR-7.7** [Must] [derived from FR-20.3, §19] Each boot shall log L1 once, before
  any other `web:` line.
- **FR-7.8** [Must] [derived from §5.3] Each entry into a connectivity state shall
  log L2 once.
- **FR-7.9** [Must] [derived from FR-13.1] Entry into PROVISIONING shall log L3.
- **FR-7.10** [Must] [derived from FR-5.15] Entry into ONLINE and T9 shall log L5.
- **FR-7.34** [Must] [derived from R-1, FR-24.13 and FR-26.23] Each line
  redrawn by D4 shall log L7 with the exact text drawn, except a countdown on
  line 5 (the setup check's or the BLE key window's), which logs L7 only when
  its minutes change (and once at 00:00).
- **FR-7.12** [Must] [derived from D2] D2 shall log L8 at error level.
- **FR-7.13** [Must] [derived from D5] D5 shall log L9 at error level.
- **FR-7.14** [Must] [derived from FR-14.6] Each status-server answer shall log L10.
- **FR-7.31** [Must] [derived from §15, FR-5.23, FR-5.24] OTA outcomes shall log
  L11 (download succeeded), L12 (request refused), L13 (image confirmed), L14
  (every boot while a slot is recorded invalid) and L16 (confirm deadline
  missed), at the levels tabled.
- **FR-7.32** [Must] [derived from FR-18.3] When the BOOT button has been held for
  5 s, the device shall log L17b once per hold.
- **FR-7.16** [Must] [derived from FR-5.21] T10 shall log L15 before restarting.
- **FR-7.17** [Must] [derived from FR-5.18] T6 and T7 shall log L6 with the delay
  actually armed.
- **FR-7.35** [Should] [user] L6 shall carry the reason the station gave for
  its last lost link or failed attempt (ESP-IDF's `wifi_err_reason_t`, e.g. 201
  no AP found, 15 handshake timeout; 0 before any), so a failed join can be told
  apart from the log alone.
- **FR-7.30** [Must] [user] The device shall log no line with the tag `hello`.

### 7.3a Testbench compatibility lines

The testbench's skills recognise device events by searching the log, unanchored,
for these exact strings. Each line is logged in addition to the `web:` line of the
same event; tag and level are free except where stated. `<SSID>` is the SSID
concerned, `<ip>` the destination of FR-16.1.

| # | Event | String the line contains |
|---|-------|--------------------------|
| K1 | T1 | `No WiFi credentials` |
| K2 | SoftAP started (T1) | `AP mode: SSID='<SoftAP SSID>'` |
| K3 | Portal `GET /` answered | `Portal page requested` |
| K4 | T3 | `Credentials saved` |
| K5 | Station attempt started (T2, T3, T8) | `STA mode, connecting to '<SSID>'` |
| K6 | T5 | `STA got IP` |
| K7 | T6, T7 | `STA disconnect, retry` |
| K8 | T10, after the erase | `WiFi credentials erased` |
| K9 | UDP log started (T5) | `UDP logging -> <ip>:5555` |
| K10 | OTA download succeeded | `OTA succeeded` |
| K11 | OTA download failed (level E) | `OTA failed: <reason>` |
| K12 | Every 10 s from the end of startup | `alive <N>`, N = 0, 1, 2, … |

- **FR-7.18** [Must] [user, testbench compatibility contract] T1 shall log K1.
- **FR-7.19** [Must] [user, testbench compatibility contract] Starting the SoftAP
  shall log K2.
- **FR-7.20** [Must] [user, testbench compatibility contract] Each portal `GET /`
  shall log K3.
- **FR-7.21** [Must] [user, testbench compatibility contract] T3 shall log K4.
- **FR-7.22** [Must] [user, testbench compatibility contract] Each station attempt
  shall log K5 when it starts.
- **FR-7.23** [Must] [user, testbench compatibility contract] T5 shall log K6.
- **FR-7.24** [Must] [user, testbench compatibility contract] T6 and T7 shall log
  K7.
- **FR-7.25** [Must] [user, testbench compatibility contract] T10 shall log K8
  after erasing the credentials and before restarting.
- **FR-7.26** [Should] [user, testbench compatibility contract] Starting the UDP
  log shall log K9.
- **FR-7.27** [Must] [user, testbench compatibility contract] A successful OTA
  download shall log K10 before T11.
- **FR-7.28** [Must] [user, testbench compatibility contract] A failed OTA
  download shall log K11 at error level.
- **FR-7.29** [Must] [user, testbench compatibility contract] From the end of
  startup, the device shall log K12 every 10 s ± 1 s, N increasing by 1 per line
  from 0.

### 7.4 Verification contracts

| ID | Precondition · stimulus | Expected observation | Must NOT happen | Tier |
|----|------------------------|----------------------|-----------------|------|
| FR-7.5 | Any capture | Every `web:` line matches `[IEWD] \([0-9]+\) web: ` | A line without timestamp | target |
| FR-7.6 | USB reset; capture until `web: state=` | Exactly one `Init complete, running event-driven` | Absent; repeated; reworded | target |
| FR-7.7 | USB reset | L1 once, first `web:` line; `reason=` one of `USB`, `SW`, `POWERON`; `slot=ota_0` or `ota_1` | Missing; after another `web:` line; numeric reason | target |
| FR-7.8 – FR-7.10 | Provisioning run (FR-5.13) | L3, L4, L2 sequence PROVISIONING → CONNECTING → ONLINE, L5 with the bench-assigned IP | Missing or duplicated state line | bench |
| FR-7.8 – FR-7.17 (core) | Format each line with sample values | The §7.2 strings exactly | Any other spelling or level | host |
| FR-7.34 | ONLINE; one request | L7 `line2="Req: <n>"` | Text differing from the drawn line | bench |
| FR-7.12, FR-7.13 | LCD fault build at start / 5th write | L8 `ESP_ERR_TIMEOUT` / L9 `ESP_ERR_INVALID_STATE` at level E, once | Level I/W; numeric code; repeated | target |
| FR-7.14 | ONLINE; `GET /nope` | `web: http GET /nope 404 n=<count>` | Missing | bench |
| FR-7.30 | Inspect the shipped application image (`.bin`) | No NUL-terminated string `hello` (the tag) and no `Hello, world!` in it | Either byte string present | host |
| FR-7.30 (wire) | JRN-01 run, serial capture from reset to ONLINE plus 30 s | No line matching `[IEWD] \([0-9]+\) hello: ` | Any such line | bench |
| FR-7.16, FR-7.17, FR-7.31, FR-7.32 | Covered by the FR-5.21, FR-18.4, FR-5.24, FR-15.x and FR-5.18 runs | Lines as tabled | Missing; L17b repeated within one hold | bench |
| FR-7.18 – FR-7.22 | Provisioning run (FR-5.13) from erased NVS | K1, K2 with the FR-13.1 SSID, K3, K4, K5 with the bench SSID, in that order | A string missing, reworded or out of order | bench |
| FR-7.23, FR-7.26 | As above, to ONLINE | K6, then K9 with the bench gateway | Missing; K6 before an IP exists | bench |
| FR-7.24 | NFR-5.2 run | K7 for each failed attempt | Missing | bench |
| FR-7.25 | FR-5.21 run | K8 between L15 and the boot line | Missing; after the boot line | bench (operator) |
| FR-7.27, FR-7.28 | FR-15.10 and FR-15.11 runs | K10 / K11 as tabled | Missing; K10 for a failed download | bench |
| FR-7.29 | USB reset; 65 s capture after L17 | 6 or 7 `alive` lines, gaps 9 – 11 s, N = 0, 1, 2, … | A gap outside 9 – 11 s; N skipping | target |
| FR-7.18 – FR-7.29 (core) | Format each K line with sample values | The tabled strings exactly | Any other spelling | host |

---

## 13. Provisioning Portal

### 13.1 Purpose & peer

Lets a person (or the testbench) hand the device WiFi credentials. Peer: any
client joined to the device's SoftAP. Active in PROVISIONING only.

### 13.2 Protocol and routes

Open SoftAP, IPv4 192.168.4.1/24, DHCP server, HTTP/1.1 on TCP port 80, DNS on UDP
port 53. The form contract matches the testbench's default `enter-portal` call.

| Method | Path | Request | Response |
|--------|------|---------|----------|
| GET | `/` | — | 200 `text/html`: form with fields `ssid` and `password`, `method="post"`, `action="/connect"`, and the script that checks it (FR-13.8, FR-13.9) |
| POST | `/connect` | `application/x-www-form-urlencoded`, fields `ssid`, `password` | 200 `text/html` naming the SSID, or 400 `text/plain` naming the invalid field |
| GET | any other path | — | 302, `Location: http://192.168.4.1/` |

### 13.3 Requirements

- **FR-13.1** [Must] [user] In PROVISIONING the device shall run an open SoftAP
  named `TDS3-<XXXXXX>`, where `<XXXXXX>` is the last three bytes
  of the base MAC in upper-case hex, at 192.168.4.1, on channel 1.
- **FR-13.2** [Must] [user] `GET /` shall answer 200 with the form of §13.2.
- **FR-13.3** [Must] [user] `POST /connect` with valid fields shall answer 200
  with a page naming the submitted SSID, before T3 stops the SoftAP.
- **FR-13.4** [Must] [derived from IEEE 802.11 and WPA2] A submission shall be
  valid only if `ssid` is 1 – 32 bytes and `password` is empty or 8 – 63 bytes;
  any other submission, or one missing a field, shall answer 400 naming the field.
  With `wifi` `off` both fields may be missing (FR-34.2).
- **FR-13.5** [Should] [pack:esp32] In PROVISIONING, every DNS A query shall be
  answered with 192.168.4.1.
- **FR-13.6** [Should] [pack:esp32] A `GET` of any path other than `/` shall answer
  302 to `http://192.168.4.1/`.
- **FR-13.7** [Must] [derived from §9.1] The SSID echoed by FR-13.3 shall be
  HTML-escaped (`&`, `<`, `>`, `"`, `'`).
- **FR-13.8** [Must] [user] The form shall mark every field a submission
  requires with `*`, and the browser shall check each field against its rule
  (FR-13.4, §25.2, FR-28.1) before anything is sent: a required field left
  empty, or a value outside its length, pattern, type or range, is marked on
  the page and nothing is sent. A field of an all-or-nothing group (the mail
  fields of §25.2) becomes required once any field of the group is filled, and
  optional again when the group is emptied.
- **FR-13.9** [Must] [user] When the device answers a submission 400, the form
  shall stay on the page with every value typed kept, passwords included, and
  name the refused field by its label; the field stays marked until it is
  changed. A rule only the device checks (for example `average_min` against
  `sample_min`) is reported this way. A browser without `fetch` posts the form
  as a plain submission and sees the 400 text of §13.2.

### 13.4 Verification contracts

| ID | Precondition · stimulus | Expected observation | Must NOT happen | Tier |
|----|------------------------|----------------------|-----------------|------|
| FR-13.1 | PROVISIONING; bench scan | SSID `TDS3-` + MAC suffix from the test-plan MAC record, open | Secured AP; other name | bench |
| FR-13.2 | Bench joins SoftAP; `GET /` | 200; form with `ssid`, `password`, POST `/connect` | Other field names | bench |
| FR-13.3 | Bench `enter-portal` | 200 naming the SSID | Connection reset before the answer | bench |
| FR-13.4 (core) | Validate (ssid, password) for lengths 0/1/32/33 and 0/7/8/63/64 | Valid exactly for ssid 1 – 32 and password 0, 8 – 63 | Any other verdict | host |
| FR-13.5 | Joined to SoftAP; relay `GET http://example.com/` | Portal response (200 form or 302 to the portal) | A DNS failure | bench |
| FR-13.6 | Joined; `GET /generate_204` | 302 to `http://192.168.4.1/` | 404; 204 | bench |
| FR-13.7 (core) | Escape `<b>&"'` | `&lt;b&gt;&amp;&quot;&#39;` | Raw characters | host |
| FR-13.8 | Bench joined to the SoftAP; a phone browser on the portal: submit empty; then a 7-character password; then only `mail_host` filled; then empty `mail_host` again | Only *Network name* starred at first; nothing sent the first three times (the field marked); the seven mail fields starred with `mail_host` filled and unstarred when emptied | A request sent; a partial mail group sent | bench (operator) |
| FR-13.9 | As FR-13.8; a valid-looking form with `setup` custom, `sample_min` 10, `average_min` 7; submit; then `average_min` 60 | The page names *Custom: average them over*, every value including both passwords still filled; the second submission answers the FR-13.3 page | A field emptied; a plain-text 400 page | bench (operator) |

### 13.5 Failure modes

A request that does not parse answers 400; the portal stays up. The SoftAP is
never stopped except by T3 or a restart.

---

## 14. Status HTTP Server

### 14.1 Purpose & peer

Serves device status to LAN clients. Active in ONLINE only. HTTP/1.1, TCP port 80
on the station address.

### 14.2 Routes and schema

| Method | Path | Response |
|--------|------|----------|
| GET | `/` | 200 `text/html; charset=utf-8` showing every field of the schema below |
| GET | `/api/status` | 200 `application/json` with the schema below |
| POST | `/ota` | §15 |
| any | any other | 404 |

`/api/status` body — one JSON object, exactly these keys:

| Key | Type | Unit / format | Meaning |
|-----|------|---------------|---------|
| `fw_version` | string | FR-20.3 | Running image version |
| `free_heap` | integer | bytes | Current free heap |
| `rssi_dbm` | integer | dBm | Station RSSI |
| `ip` | string | dotted quad | Station IPv4 address |
| `hostname` | string | FR-20.1 | Without `.local` |
| `requests` | integer | count | FR-14.6, including this request |

### 14.3 Requirements

- **FR-14.7** [Must] [user] In ONLINE, `GET /` shall answer 200 with an HTML page
  showing every §14.2 field and no uptime.
- **FR-14.8** [Must] [user] In ONLINE, `GET /api/status` shall answer 200 with a
  JSON object having exactly the six §14.2 keys and their types.
- **FR-14.3** [Must] [derived from FR-14.7/14.8] The HTML page and the JSON object
  shall report the same `fw_version`, `ip` and `hostname`.
- **FR-14.4** [Must] [derived] Any other method/path combination than §14.2 shall
  answer 404.
- **FR-14.5** [Must] [derived from §5.3] Outside ONLINE, nothing shall listen on
  TCP port 80 of the station interface.
- **FR-14.6** [Must] [user] The request count shall start at 0 at boot and increase
  by exactly 1 for each answered request, whatever its status.
- **NFR-14.1** [Must] [user] With one client issuing 100 sequential `GET /`
  requests, the 95th-percentile time from request to complete response shall be
  ≤ 500 ms.

### 14.4 Verification contracts

| ID | Precondition · stimulus | Expected observation | Must NOT happen | Tier |
|----|------------------------|----------------------|-----------------|------|
| FR-14.7 | ONLINE; relay `GET /` | 200, `text/html`, page contains each value from a `/api/status` taken right after | A missing field; any uptime value or label (`uptime`, `Up:`) | bench |
| FR-14.7 (core) | Render the HTML page with sample values | Every §14.2 value present | `uptime` or `Up:` anywhere in the page | host |
| FR-14.8 | ONLINE; relay `GET /api/status` | Parses as JSON; exactly the 6 keys with the tabled types; `ip` equals the bench-assigned address | Extra key (incl. `uptime_s`); missing key; string where integer | bench |
| FR-14.8 (core) | Render the JSON with sample values incl. 0 and 2³² − 1 | Valid JSON with the 6 keys, values verbatim | `uptime_s`; truncation; invalid JSON | host |
| FR-14.3 | As FR-14.7 | Equal values | Any difference | bench |
| FR-14.4 | `GET /nope`; `DELETE /api/status` | 404 each | 200; 500 | bench |
| FR-14.5 (core) | Transition function over every §5.3 row | Server-start action only in T5; server-stop action in T7; none elsewhere | A start in any other row; no stop in T7 | host |
| FR-14.5 | PROVISIONING; bench joined to the SoftAP; bench port scan of 192.168.4.1 | Port 80 is the portal (§13.2), and `GET /api/status` answers 302 (FR-13.6), not the status JSON | The status JSON outside ONLINE | bench |
| FR-14.6 | ONLINE; 3 sequential `GET /api/status` | `requests` = k, k+1, k+2 | Skip or repeat | bench |
| NFR-14.1 | ONLINE; 100 sequential relay `GET /` | p95 ≤ 500 ms (relay time included) | A 5xx; p95 > 500 ms | bench |

---

## 15. OTA Receiver

### 15.1 Purpose & peer

Installs a new application image that a LAN client names by URL. Peers: the
requesting client (route `POST /ota` on the status server, §14, ONLINE only) and
the HTTP server holding the image (on the bench, the testbench's firmware route
`http://<gateway>:8080/firmware/<project>/<file>`).

Request: `application/json`, body `{"url":"http://<host>[:<port>]/<path>"}`. The
answer is immediate; the download runs afterwards and its outcome is reported in
the log (K10, K11) and, on success, by the restart (T11).

### 15.2 Requirements

- **FR-15.1** [Must] [user] The receiver shall write the image only to the
  application slot that is not running.
- **FR-15.12** [Must] [derived] A `POST /ota` while a download is in progress
  shall answer 409.
- **FR-15.13** [Must] [derived from §15.1, ONLINE only] When the station
  disconnects during a download, the device shall abort the download, log K11
  and leave the boot slot unchanged (T7).
- **NFR-15.1** [Must] [user] A `POST /ota` shall be answered within 2 s of the
  request's last byte.
- **NFR-15.2** [Must] [user] A download that fails for any reason other than a
  stall shall log K11 within 60 s of its `POST /ota`.
- **FR-15.8** [Must] [user] A `POST /ota` whose body is a JSON object with a `url`
  string starting `http://` and at most 256 characters long shall
  answer 202 with body `{"ok":true}` and then start the download.
- **FR-15.9** [Must] [derived] A `POST /ota` whose body is not such an object
  shall answer 400 and start no download.
- **FR-15.10** [Must] [user] A download that answers 200 and yields a valid
  ESP32-S3 application image of at most 0x400000 bytes shall select that image
  for the next boot and log K10 (then T11).
- **FR-15.11** [Must] [user] A download that answers other than 200, breaks off,
  receives no byte for 30 s, exceeds 0x400000 bytes, or yields an image
  that fails ESP-IDF validation shall log K11, leave the boot slot unchanged, and
  not restart the device (T12).

### 15.3 Verification contracts

| ID | Precondition · stimulus | Expected observation | Must NOT happen | Tier |
|----|------------------------|----------------------|-----------------|------|
| FR-15.1 | Note L1's `slot=`; request a valid hosted image; then request it again | After each OTA, L1's `slot=` is the other slot (`ota_0` ↔ `ota_1`), each boot ONLINE and confirmed | The same slot after an OTA; a boot with `fw=` other than the requested build | bench |
| FR-15.12 | ONLINE running build A; host build A's own image (its download lasts longer than 1 s on the bench); request it; request again within 1 s | Second answer 409; first download completes and re-installs fw=A (T11) | Two downloads; the second answered 202 | bench |
| FR-15.12 (core) | Download state machine: second start while busy | Refused with 409 | Accepted | host |
| FR-15.13 | ONLINE running build A; host a build B whose download lasts longer than 2 s on the bench; request it; stop the bench AP 2 s after the bench log shows the fetch starting; restart the AP after 20 s | K11 and `web: state=WIFI_LOST`; after the AP returns, `web: online`, `/api/status` reports `fw_version` = A, and a following USB reset boots `fw=A` on the same `slot=` | K10; a restart into B; `OTA succeeded` after the disconnect | bench |
| NFR-15.1 | ONLINE; 5 × `POST /ota` (valid URL, then 4 invalid bodies of FR-15.9) | Each answered ≤ 2 s after sending, by relay timing | An answer after 2 s; an answer only after the download | bench |
| FR-15.8 | ONLINE; `POST /ota` with a hosted image URL | 202 `{"ok":true}` (NFR-15.1 timing); the bench activity log shows the device fetching the file | 200 with a delayed answer after the download; no fetch | bench |
| FR-15.9 (core) | Parse bodies: not JSON; `{}`; `{"url":5}`; `https://…`; 257-character URL; valid | 400 for each invalid; accepted only for the valid one | Any invalid body accepted | host |
| FR-15.9 | ONLINE; `POST /ota` with `{"url":"ftp://x"}` | 400; L12; no fetch | 202 | bench |
| NFR-15.2 | As the FR-15.11 full contract (bad bytes, cut image, oversize, 404) | K11 ≤ 60 s after each `POST /ota`, by relay send time and serial capture | No K11 within 60 s + 3 s (Appendix A); K10 | bench |
| FR-15.10 | Host the CI artefact of a version-bumped build; request it | K10, then FR-5.32's restart into the new `fw=` | K11; old `fw=` | bench |

Full form:

```yaml
id: FR-15.11
verification:
  preconditions:
    - Device ONLINE running build A; bench hosts (1) 300 kB of random bytes, (2) build A's image cut to its first 300 kB, (3) a 4194305-byte file starting with a valid image header.
  stimulus:
    - Request each of (1), (2), (3) in turn with POST /ota; then request a URL that answers 404; then USB reset.
  expected_observations:
    - Each request answered 202, followed by K11 for each download.
    - "web: online" continues; GET /api/status still answers with fw_version = A after every attempt.
    - After the USB reset, the boot line shows fw=A and the same slot= as before the attempts.
  timing: K11 within 60 s of each request (NFR-15.2)
  tolerance: none
  prohibited_outcomes:
    - Any restart before the USB reset.
    - K10 for any of the four.
    - A boot into the other slot after the USB reset (the slot was switched).
  tier: bench
  evidence:
    - Bench activity log of the four fetches; serial capture through the USB reset.
  cleanup:
    - Device running build A, ONLINE; hosted test files deleted.
```

```yaml
id: FR-15.11 (stall)
verification:
  preconditions:
    - Device ONLINE running build A; a bench-side HTTP server that sends the headers and 100 kB of build B, then holds the connection open without sending.
  stimulus:
    - POST /ota with that server's URL; wait 45 s.
  expected_observations:
    - K11 30 s – 33 s after the last byte was sent (30 s stall timeout, up to + 3 s late, Appendix A).
    - A follow-up request for build A's own image is answered 202 (the receiver recovered), followed by K10 and a restart into fw=A (T11).
  timing: 30 s stall detection
  tolerance: + 3 s, never early
  prohibited_outcomes:
    - K11 before 30 s (stall timeout too short).
    - A restart before the follow-up request is sent.
    - K11 later than 33 s after the last byte, or none within 45 s (download hangs).
    - 409 for the following request (receiver stuck "in progress").
  tier: bench
  evidence:
    - Serial capture; bench-side server log with the last-byte timestamp.
  cleanup:
    - Device running build A, ONLINE and confirmed.
```

---

## 16. UDP Log

### 16.1 Purpose & peer

Mirrors the `web:` log lines over the network so a bench run can observe the
device without the serial port. Peer: the station's default gateway (on the bench,
the testbench Pi), UDP port 5555.

### 16.2 Requirements

- **FR-16.1** [Should] [pack:esp32] In ONLINE, every `web:` line of level I or
  above shall be sent as one UDP datagram to the station's default gateway on port
  5555.
- **FR-16.2** [Should] [pack:esp32] Each datagram's text shall equal the serial
  line's text from `web:` to the end of the line, without the ESP-IDF prefix or
  colour codes.
- **FR-16.3** [Should] [derived] With no receiver on the gateway, serial logging
  and HTTP answers shall continue unaffected.

### 16.3 Verification contracts

| ID | Precondition · stimulus | Expected observation | Must NOT happen | Tier |
|----|------------------------|----------------------|-----------------|------|
| FR-16.1, FR-16.2 | ONLINE; clear the bench UDP log; 5 `GET /api/status` | 5 entries `web: http GET /api/status 200 n=<k>` from the DUT's IP, equal to the serial lines' text | Missing entries; prefix or ANSI codes in the text | bench |
| FR-16.2 (core) | Strip a sample prefixed, coloured line | `web: …` text only | Residual prefix/escape bytes | host |
| FR-16.3 | Bench UDP listener stopped; 20 `GET /api/status` | 20 answers 200, 20 serial L10 lines | Missing answers or lines; restart | bench |

---

## 17. mDNS Responder

### 17.1 Purpose & peer

Makes the device reachable by name on the LAN. Uses the managed `espressif/mdns`
component. Active in ONLINE only.

### 17.2 Requirements

- **FR-17.1** [Should] [pack:esp32] In ONLINE, the device shall answer mDNS A
  queries for `<hostname>.local` (FR-20.1) with its station IPv4 address.
- **FR-17.2** [Should] [derived from FR-20.1] The device shall send its hostname
  in its DHCP request.

### 17.3 Verification contracts

| ID | Precondition · stimulus | Expected observation | Must NOT happen | Tier |
|----|------------------------|----------------------|-----------------|------|
| FR-17.1 | ONLINE; relay `GET http://<hostname>.local/api/status` | 200, `ip` equals the device address | Name not resolved | bench |
| FR-17.2 | Bench AP started; device joins | The bench WiFi event `STA_CONNECT` for the DUT's MAC carries `hostname` = FR-20.1's name | Default `espressif` hostname | bench |

---

## 18. BOOT Button

### 18.1 Purpose & peer

The on-board BOOT button (GPIO 0, active low): a long
press requests a factory reset, which happens on release, so the button is never
held while the chip resets (R-9). Sampled only after BOOT exits.

### 18.2 Requirements

- **FR-18.3** [Must] [user] Releasing the button after holding it continuously
  for at least 5 s shall raise exactly one factory-reset event (§5.3 T10), at the
  release.
- **FR-18.4** [Must] [derived from FR-18.3] While the button is held, the device
  shall raise no factory-reset event and shall not restart.
- **FR-18.5** [Must] [derived] Releasing the button before 5 s shall
  raise no factory-reset event; outside the setup-check hold it changes
  nothing, inside it the press ends the hold (FR-24.11).

### 18.3 Verification contracts

| ID | Precondition · stimulus | Expected observation | Must NOT happen | Tier |
|----|------------------------|----------------------|-----------------|------|
| FR-18.3 | See FR-5.21 (full form) | L17b, then L15 and a boot line after release | A reset while held (ROM download-mode banner, no boot line) | bench (operator) |
| FR-18.3 (core) | Hold detector fed samples: low 4.9 s then high; low 5.0 s then high; low 12 s then high; low 30 s with no release | No event; one event at the release; one event at the release; no event | An event while still low; an event under 5 s; two events for one hold | host |
| FR-18.4 | ONLINE; operator holds BOOT for ~12 s, then releases | L17b about 5 s into the hold; no L15 and no boot line before the release (operator confirms the release time) | L15 or a restart while held | bench (operator) |
| FR-18.5 | ONLINE, no setup check pending; operator presses BOOT for ~2 s | No L15, no boot line, still ONLINE 10 s later | Factory reset; restart | bench (operator) |

---

# Part C — Foundation (L0)

## 8. ESP-IDF Platform

### 8.1 Purpose

ESP-IDF v6.0.2 supplies the WiFi driver (SoftAP and station), esp_netif/lwIP with
DHCP server and client, esp_http_server, nvs_flash, app_update and the
rollback-capable bootloader, esp_task_wdt, esp_lcd's SPI panel IO for the RM67162, the GPIO
driver, esp_timer, FreeRTOS, esp_log and the USB-Serial/JTAG console; the managed
`espressif/mdns` component supplies mDNS. Configured and used, not implemented.

### 8.2 Requirements

None of its own. Exercised **transitively** through §5 – §7 and §13 – §20; no
host-tier tests.

---

# Part D — Cross-cutting Concerns

## 9. Security

### 9.1 Security profile: trusted LAN

| Aspect | Position |
|--------|----------|
| Threat actors | Considered: none on the LAN (trusted, [user]). Nearby radio users during provisioning: accepted |
| Physical access | Full control (accepted) |
| Remote exposure | BLE GATT service inside a setup window only (§26): device info readable by anyone in range, settings only after passkey pairing; HTTP on port 80 (status, OTA request) in ONLINE; outbound HTTP to whatever URL an OTA request names; open SoftAP with portal in PROVISIONING; mDNS; outbound UDP logs; outbound SMTP over TLS to the configured mail server (§25); outbound HTTP or HTTPS to the configured ntfy server (§31); outbound HTTP or HTTPS to the configured ThingsBoard server (§32); with `uplink` `cell`, notes over I2C to the Notecard, which sends them to Blues Notehub (§33); with `wifi` `off`, nothing over WiFi at all (§34) except ESP-NOW; with `espnow_role` not `off`, ESP-NOW frames encrypted and authenticated with `espnow_key` and, on a gateway, MQTT to the ThingsBoard host (§38) |
| Confidentiality | **Not claimed.** HTTP and UDP logs are plaintext; NVS is not encrypted, so the WiFi password is readable from flash |
| Integrity / authenticity of firmware | **Not claimed.** Anyone on the LAN can upload an image; no signature check; secure boot off |
| Credentials | One WiFi password, one mail password, one ThingsBoard access token, an optional ntfy token and an optional ESP-NOW network key, stored in NVS; never output by the device (FR-9.3, FR-25.10, FR-31.13, FR-32.3, FR-38.2) |

Accepted risks [user]: readings and the ThingsBoard token sent in plain text when `tb_url` is `http://` (§32); unauthenticated OTA (anyone on the LAN can make the device
download and run any image from any HTTP URL); open portal SoftAP (anyone in radio
range during PROVISIONING can submit credentials or read submitted ones);
plaintext HTTP and logs; unencrypted NVS; anyone who can read the panel
during a BLE window can pair and change settings; alerts to an `http://`
ntfy server are readable on the network path, and on a public server by
anyone who knows the topic. The profile justifies these protections
only: never disclosing the passwords, keeping the radio surface to what the
product uses, and not reflecting portal input as markup (FR-13.7).

### 9.2 Requirements

- **FR-9.3** [Must] [derived from §9.1] The device shall not output the stored
  WiFi password on serial, UDP, or any HTTP response.
- **FR-9.4** [Must] [derived from §9.1] The SoftAP shall be active only in
  PROVISIONING.
- **FR-9.5** [Must] [derived from §9.1] In ONLINE, TCP port 80 shall be the only
  open TCP port on the station address.

### 9.3 Verification contracts

| ID | Precondition · stimulus | Expected observation | Must NOT happen | Tier |
|----|------------------------|----------------------|-----------------|------|
| FR-9.3 | Provision with password `pw-<random 12 hex>`; capture serial and UDP through ONLINE; fetch `/` and `/api/status` | The marker appears nowhere | The marker in any capture or body | bench |
| FR-9.4 | ONLINE for 30 s, then a 60 s bench-AP outage (the device alternates CONNECTING and WIFI_LOST); a bench scan every 5 s throughout | No FR-13.1 SSID in any scan | The SSID seen | bench |
| FR-9.5 | ONLINE; bench port scan TCP 1 – 65535 of the device | Open: 80 only | Any other open port | bench |

---

## 10. Build & Platform Constraints

The numbers a build will otherwise take from a default.

- **C-10.1** [Must] [user] The firmware shall be built for target `esp32s3`.
- **C-10.2** [Must] [user] The firmware shall be built with ESP-IDF v6.0.2.
- **C-10.3** [Must] [user] The build shall declare a 16 MB flash size; a unit with
  less than 16 MB of flash cannot run this firmware.
- **C-10.5** [Must] [derived from SLOT1's unproven EN/BOOT] The firmware shall be
  flashable, resettable and observable through the USB-Serial/JTAG port alone,
  with the console on USB-Serial/JTAG.
- **C-10.6** [Must] [user: two 4 MB slots; offsets derived] The partition table
  shall be exactly:

  | Name | Type / subtype | Offset | Size |
  |------|----------------|--------|------|
  | nvs | data / nvs | 0x9000 | 0x6000 |
  | otadata | data / ota | 0xF000 | 0x2000 |
  | phy_init | data / phy | 0x11000 | 0x1000 |
  | ota_0 | app / ota_0 | 0x20000 | 0x400000 |
  | ota_1 | app / ota_1 | 0x420000 | 0x400000 |

  No factory partition; flash above 0x820000 is unused.
- **C-10.10** [Must] [user: two 4 MB slots, a 2 MB history; offsets derived]
  The partition table shall be exactly the rows of C-10.6 and a history
  partition:

  | Name | Type / subtype | Offset | Size |
  |------|----------------|--------|------|
  | nvs | data / nvs | 0x9000 | 0x6000 |
  | otadata | data / ota | 0xF000 | 0x2000 |
  | phy_init | data / phy | 0x11000 | 0x1000 |
  | ota_0 | app / ota_0 | 0x20000 | 0x400000 |
  | ota_1 | app / ota_1 | 0x420000 | 0x400000 |
  | history | data / 0x40 | 0x820000 | 0x200000 [user] |

  No factory partition; flash above 0xA20000 is unused.
- **C-10.7** [Must] [derived from C-10.6] The application image shall be at most
  0x400000 bytes.
- **C-10.8** [Must] [derived from FR-5.24] The bootloader shall be built with app
  rollback enabled.

| ID | Precondition · stimulus | Expected observation | Must NOT happen | Tier |
|----|------------------------|----------------------|-----------------|------|
| C-10.1 | Inspect `sdkconfig` after build | `CONFIG_IDF_TARGET="esp32s3"` | Other target | host |
| C-10.2 | Inspect the build log / CI image tag | `v6.0.2` | Other version | host |
| C-10.3 | Inspect `sdkconfig` | `CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y` | Other size; default | host |
| C-10.3 (board) | Read the flash ID in SLOT1 | 16 MB | Smaller | bench |
| C-10.5 | Flash and reset SLOT1 using only USB | Flash succeeds; L1 and L17 appear | Need for EN/BOOT or a button | bench |
| C-10.10 | Parse the built partition table binary | Exactly the six rows tabled | Any other row, offset or size | host |
| C-10.7 | `idf.py size` / image file size | ≤ 0x400000 bytes | Larger | host |
| C-10.8 | Inspect `sdkconfig` | `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` | Absent | host |

---

## 19. Watchdog

### 19.1 Purpose

Turns a hung application or HTTP server task into a restart, so a hang never needs
a site visit. Uses the ESP-IDF task watchdog.

### 19.2 Requirements

- **FR-19.1** [Must] [pack:esp32] If the application task (§2.3) runs no loop
  iteration for 10 s, the device shall restart with reset reason
  `TASK_WDT`.
- **NFR-19.1** [Must] [user] A watchdog restart shall occur within 5 s after the
  10 s timeout expires, i.e. 10 s – 15 s after the watched task stops.
- **FR-19.2** [Must] [pack:esp32] If an HTTP handler blocks for 10 s,
  the device shall restart with reset reason `TASK_WDT`.

### 19.3 Verification contracts

Test build: **hang build** — the shipped firmware with a test-only trigger that
blocks, indefinitely, the application task 20 s after L17 (variant `app`) or an
HTTP handler on `GET /hang` (variant `http`). At the moment it blocks it logs
`hang: application task blocked` or `hang: http handler blocked`, the anchor the
watchdog timing is measured from.

```yaml
id: FR-19.1
verification:
  preconditions:
    - Hang build, variant app. From Phase 2 on, the device is provisioned.
  stimulus:
    - USB reset; the build blocks its application task 20 s after L17.
  expected_observations:
    - A boot line "web: boot reason=TASK_WDT".
    - From Phase 2 on, that boot enters CONNECTING, not PROVISIONING.
  timing: TASK_WDT reset 10 s – 15 s after the `hang:` line (NFR-19.1), both read on the bench capture clock
  tolerance: + 3 s late (Appendix A), never early; the bench capture clock resolves to ± 1 s, so a reset measured at 9 s or later counts as not early
  prohibited_outcomes:
    - A restart measured before 9 s (10 s less the ± 1 s clock resolution: timeout too short).
    - No restart within 18 s of the hang (watchdog absent or too slow).
    - Reset reason other than TASK_WDT (e.g. PANIC from a crash unrelated to the watchdog).
    - "web: state=PROVISIONING" after the restart.
  tier: target
  evidence:
    - Serial capture from the reset through the TASK_WDT boot's L17 and, from Phase 2 on, its `web: state=CONNECTING`.
  cleanup:
    - Reflash the shipped build.
```

| ID | Precondition · stimulus | Expected observation | Must NOT happen | Tier |
|----|------------------------|----------------------|-----------------|------|
| NFR-19.1 | As FR-19.1 (target) and FR-19.2 (bench) | Restart 10 s – 15 s after the `hang:` line, on the bench capture clock (± 1 s resolution) | Earlier than 10 s − 1 s; later than 15 s + 3 s | target, bench |
| FR-19.2 | Hang build, variant http; ONLINE; relay `GET /hang` | Boot line `reason=TASK_WDT` 10 – 15 s later (NFR-19.1); ONLINE again | No restart; reason ≠ TASK_WDT | bench |
| FR-19.1 (negative) | Shipped build; ONLINE; 100 requests plus a full OTA download | No `TASK_WDT` boot | A watchdog reset during normal load or OTA | bench |

---

## 20. Device Identity

### 20.1 Requirements

- **FR-20.1** [Must] [pack:esp32] The hostname shall be `tds3-<xxxxxx>`, where
  `<xxxxxx>` is the last three bytes of the base MAC in lower-case hex.
- **FR-20.2** [Must] [derived] The SoftAP SSID suffix (FR-13.1) and the hostname
  suffix shall come from the same base MAC bytes.
- **FR-20.3** [Must] [user] The firmware version reported by L1 and `fw_version`
  shall be the image's embedded application version, which a release build takes
  from its git tag.

### 20.2 Verification contracts

| ID | Precondition · stimulus | Expected observation | Must NOT happen | Tier |
|----|------------------------|----------------------|-----------------|------|
| FR-20.1, FR-20.2 | ONLINE; `/api/status`; bench MAC record `34:85:18:71:c7:34` | `hostname` = `tds3-71c734`; portal SSID was `TDS3-71C734` | Other suffixes | bench |
| FR-20.1 (core) | Format for MAC `..:ab:cd:ef` | `tds3-abcdef`, `TDS3-ABCDEF` | Other case or order | host |
| FR-20.3 | Build tagged `v0.1.0` in CI; flash; reset | L1 `fw=0.1.0` (or `v0.1.0`, as the build embeds it); same in `/api/status` | The two disagree; a default `1` | bench |

---

## 21. Deep Sleep

### 21.1 Purpose & peer

Between tasks the device sleeps, so it can run from a battery, like a remote
weather station that wakes, reads its sensors, reports (e.g. over cellular) and
sleeps again. After a boot that is not a sleep wake, and after a key wake, it is
fully up for a window (panel, WiFi, web page, OTA; the bench can reach it); then
it runs its task (read a sensor and act on the data; the task itself is not yet
specified), which tells it to sleep for a period of the task's choosing. A timer
wake is the lean path: the task runs with the panel dark and WiFi off, then
sleeps again. Waking from deep sleep is a
reboot. Built on the shared `deep_sleep` component; the logic layer (§5)
decides when to sleep, the composition root performs it.

The user key is GPIO 21, active low, an RTC GPIO (LilyGO factory
`pins_config.h`); GPIO 0 (BOOT) stays the factory-reset button (§18).

### 21.2 Requirements

- **FR-21.1** [Must] [user] When told to go back to sleep with a period of
  N seconds, the device shall enter deep sleep and wake N s later.
- **FR-21.2** [Must] [user] After a boot that is not a deep-sleep wake, the
  device shall stay awake for at least 60 s before it sleeps.
- **FR-21.3** [Must] [derived from FR-21.2 and T13] The 60 s window shall apply
  to every boot that is not a timer wake (power-on, reset, OTA restart, key wake,
  watchdog), so an image pending verify is confirmed or rolled back before any
  sleep and the bench can always flash the device.
- **FR-21.4** [Must] [user] After the window, and after every timer wake, the
  device shall run its task, then sleep when told to.
- **FR-21.9** [Must] [user] The task shall
  tell the device to sleep and choose the period; nothing outside the firmware
  (HTTP, portal) can.
- **FR-21.13** [Must] [user] After a timer
  wake the device shall leave the panel dark and the WiFi radio off, run the
  task, and sleep again; no window, no web server. The one exception is a
  summary email due on that wake (FR-25.6), for which WiFi runs as a station
  only while it is sent; the second is an alert due on that wake (FR-31.10),
  likewise; the third is a ThingsBoard upload due on that wake (FR-32.5),
  likewise; the fourth is the wake whose reading ends the last
  test (FR-28.11), which continues as after a key wake; the fifth, with
  `uplink` `espnow`, is the ESP-NOW send of FR-38.10, for which the WiFi
  driver runs in station mode without joining a network only while it sends
  [proposed].
- **FR-21.11** [Must] [user] A key wake
  shall be handled like a power-on boot: the 60 s window, panel, WiFi and web
  server (with `wifi` `on`, FR-34.9), then the task (local maintenance and OTA).
- **FR-21.12** [Must] [user] The device shall not sleep before WiFi
  provisioning is complete: while no credentials are stored (PROVISIONING;
  a stored `wifi` `off` completes it, FR-34.5), the task does not run,
  whatever the time since boot.
- **FR-21.5** [Must] [user] Pressing the user key (GPIO 21) during sleep shall
  wake the device before the period ends.
- **NFR-21.1** [Must] [user] Arming the key wake shall add no more than
  **5 µA** to the sleep current.
- **FR-21.6** [Must] [user] During sleep the panel shall be powered fully off:
  dark, display off, sleep in, then its power GPIO (38) held low.
- **FR-21.7** [Must] [derived] Before sleeping, a station shall leave its AP
  (radio stopped), so the AP sees the station go.
- **FR-21.8** [Must] [derived] The device shall log L18 as the last `web:` line
  before sleep, and L19 right after L1 on a deep-sleep wake (§7.2).

### 21.3 Verification contracts

| ID | Precondition · stimulus | Expected observation | Must NOT happen | Tier |
|----|------------------------|----------------------|-----------------|------|
| FR-21.1 (core) | `deep_sleep` core: periods 0, 1, max, max + 1; wake causes timer, EXT1 on GPIO 21, EXT1 elsewhere, none | Valid only within the limits; TIMER, BUTTON, NONE, NONE | An out-of-range period accepted | host |
| FR-21.1 | Sleep test build: told to sleep 10 s once the window ends | `web: sleep s=10`, then L1 and `web: wake cause=timer` 10 – 13 s later | Any line during the 10 s | target |
| FR-21.2, FR-21.3 | Reset over USB | No `web: sleep` earlier than 60 s after L1 | A sleep inside the window | target |
| FR-21.12 (core) | Task due check with no credentials stored, 600 s after boot | Not due; due once credentials are stored | The task running in PROVISIONING | host |
| FR-21.5, FR-21.11 | Asleep for 300 s; operator presses the user key | L1 and `web: wake cause=button` within 3 s of the press, then L2 and no `web: sleep` for 60 s | Waking only at the period's end; sleeping inside the window | bench (operator) |
| FR-21.13 | Sleep test build, timer wake | `web: wake cause=timer`, then `web: sleep` with no L2, L5 or L7 between | WiFi started, panel lit, the window applied | target |
| FR-21.6 | Asleep | Operator confirms the panel is dark | A lit or flickering panel | bench (operator) |
| NFR-21.1 | Sleep current with and without the key wake armed | Difference ≤ 5 µA | — | bench (needs a current meter, R-17) |

---

## 22. Time

### 22.1 Purpose & peer

Readings carry the date and time they were taken. The chip's clock keeps
running through deep sleep but starts from zero after a power loss or reset,
so it is set from the network whenever one is up: SNTP over WiFi today, and
the same rule applies to any later network (e.g. cellular network time). The
time zone turns the synced UTC clock into local time.

### 22.2 Requirements

- **FR-22.1** [Must] [user] Whenever a network is up, the device shall sync its
  clock from it: over WiFi, by SNTP from **`pool.ntp.org`** on each
  entry into ONLINE.
- **FR-22.2** [Must] [user] The device shall apply a time zone to the synced
  clock: **a POSIX TZ string fixed at build time, `EST5EDT,M3.2.0,M11.1.0`** (US Eastern, daylight
  saving from the second Sunday in March to the first Sunday in November).
- **FR-22.3** [Must] [user] A reading taken before the first sync since the
  last power-on or reset shall be marked unsynced.
- **FR-22.4** [Must] [derived from FR-22.3 and §21] A sync shall stay valid
  across deep sleep (the clock keeps running), so readings after a timer wake
  are synced if any sync happened since power-on.
- **NFR-22.1** [Must] [user] The clock shall be synced within **10 s** of
  entering ONLINE when the time server is reachable.
- **FR-22.5** [Must] [derived] The device shall log L20 on each sync (§7.2).
- **FR-22.6** [Must] [user] A reading whose clock was never set (a time before
  2020-01-01, as after a power loss with no sync yet) shall show no time: `no
  time` on the page, `null` as its `time` in `/api/readings`, `at=none` in L21.
  A set but unsynced clock keeps its time, marked unsynced (FR-22.3).

### 22.3 Verification contracts

| ID | Precondition · stimulus | Expected observation | Must NOT happen | Tier |
|----|------------------------|----------------------|-----------------|------|
| FR-22.6 (core) | Render a reading taken at 80 s and at 2019-12-31T23:59:59Z, unsynced; one at 2020-01-01T00:00:00Z, unsynced | `at=none`, `"time":null`, `no time (unsynced)`; the last keeps its time | A 1969 or 1970 date shown |
| FR-22.3, FR-22.4 (core) | Synced flag: power-on; a sync; a timer wake after a sync; a reset after a sync | unsynced; synced; synced; unsynced | A reading marked synced before any sync | host |
| FR-22.1, NFR-22.1, FR-22.5 | Device reaches ONLINE on an AP with internet | L20 within 10 s of L5, with a year ≥ 2026 | No L20 | bench (needs internet on the bench AP, R-19) |
| FR-22.2 | Shipped build (US Eastern) | L20 shows local time with offset `-0500` (EST) or `-0400` (EDT) | `+0000`; UTC shown as local | bench |

---

## 23. Weather Sensor

### 23.1 Purpose & peer

The device's task (§21) reads a Bosch BME280 temperature, humidity and pressure
sensor. On a timer wake it takes a reading and keeps it; on a boot with a window
it also shows the newest reading on the panel and the web page, and serves the
kept readings as JSON. Built on the shared `bme280` component (pure
compensation core, thin I2C layer); the logic layer decides when to read, the
composition root performs the read and routes the result.

The sensor is a genuine BME280 (chip ID `0x60`) on I2C at address **0x76**, or
**0x77** where a board's default puts it there (Adafruit's breakout),
SDA = GPIO 43, SCL = GPIO 44 (the board's UART0 pins; the console is the
USB-Serial/JTAG, §2.2). It is read in forced mode with the Bosch "weather
monitoring" settings: one measurement per reading, oversampling ×1 for
temperature, pressure and humidity, IIR filter off. Between readings the
sensor is in its sleep mode.

A reading is the triple temperature (shown in °F; the sensor reports °C),
relative humidity (%) and pressure (mbar), with the time it was taken and the synced mark of FR-22.3.

How often a reading is taken and how long each average runs are set in the
provisioning portal next to the WiFi credentials (§13.2), with email on or off:

| Field | Format | Valid | Empty or absent |
|-------|--------|-------|-----------------|
| `sample_min` | minutes between readings | whole minutes, 1 – 60 | 10 |
| `average_min` | minutes each average runs | whole minutes, greater than `sample_min`, a whole multiple of it, and dividing 1440 | 60 |

Average periods start at local midnight (FR-22.2) and follow each other, so
with 60 they run 00:00, 01:00, 02:00 and so on. These two fields are the
`custom` setup's; the setup in use is chosen from the list of §28.

### 23.2 Readings route

| Method | Path | Response |
|--------|------|----------|
| GET | `/api/readings` | 200 `application/json`: `{"readings": [ … ]}`, newest first, each element exactly the keys below |

| Key | Type | Unit / format | Meaning |
|-----|------|---------------|---------|
| `time` | string or null | `<YYYY-MM-DD>T<hh:mm:ss><±hhmm>`, local time (FR-22.2); null for a clock never set (FR-22.6) | When the reading was taken |
| `synced` | boolean | | FR-22.3: false if no sync had happened since power-on or reset |
| `temp_f` | number | °F, 2 decimals | Temperature |
| `rh_pct` | number | %, 1 decimal | Relative humidity |
| `p_mbar` | number | mbar, 2 decimals | Pressure |
| `ext` | object | one key per enabled add-on (§29.2), a number or null | Add-on values; `{}` when every add-on is off |

### 23.3 Requirements

- **FR-23.1** [Must] [user] Each time the task runs (§21, FR-21.4) it shall take
  one reading from the BME280 at 0x76 on SDA GPIO 43 / SCL GPIO 44, in forced
  mode, oversampling ×1 for all three quantities, IIR filter off. When nothing
  answers at 0x76 it shall use 0x77 (a board left at its default, e.g.
  Adafruit's) [user], trying first the address that answered last.
- **FR-23.17** [Must] [user] After its reading the task shall tell the
  device to sleep for the reading period of the normal setup (§28.1), or 30 s
  during `cycle` (FR-28.6) (FR-21.9). The portal and BLE shall refuse
  `sample_min` and `average_min` that are not valid per §23.1 when `custom`
  is chosen (FR-28.2).
- **FR-23.3** [Must] [derived from FR-23.12, FR-23.7] On every boot that is not a
  timer wake, the device shall also take a reading before the window opens, so
  the panel and the web page have a value during the window.
- **FR-23.4** [Must] [derived] Before its first reading after a boot the device
  shall check the chip ID is `0x60` and load the sensor's calibration. A sensor
  that does not answer, answers with another ID, or fails a transfer shall be
  reported (L22); no reading is kept, and the window, web server and sleep
  carry on as if the sensor were absent. The device shall not restart or retry
  within the same task run.
- **FR-23.5** [Must] [user] Each reading shall be logged as L21 (§7.2); in
  ONLINE it reaches the UDP log by FR-16.1.
- **FR-23.12** [Must] [user] While the panel is lit, line 4 of the screen shall
  show the newest kept reading (the live reading during the setup check,
  FR-24.3) as `<t>F Hum <rh>% <p> mbar`, temperature 1 decimal, humidity a
  whole number, pressure 2 decimals, e.g. `75.7F Hum 47% 996.62 mbar`; or
  `Sensor --` when none is kept.
- **FR-23.7** [Must] [user] In ONLINE, `GET /` shall also show the newest kept
  reading with its time and synced mark, or say that none is kept.
- **FR-23.18** [Must] [user] In ONLINE, `GET /api/readings` shall answer 200 with
  the §23.2 object holding every kept reading, newest first, each with its
  `ext` object (§29.2); `[]` when none is kept.
- **FR-23.9** [Must] [user] The device shall keep the newest **144** readings
  (24 h at the default 10-minute `sample_min`) across deep sleep in RTC memory, dropping the oldest when
  full. A power loss or reset empties them.
- **FR-23.15** [Must] [user] Each reading taken with the clock set (FR-22.6)
  shall be added to the average period it falls in; the first reading of a
  later period finishes the one in progress, as the mean temperature, humidity
  and pressure of its readings with the period's start time and the number of
  readings. A period with no readings has no average.
- **FR-23.16** [Must] [user] The device shall keep the newest **96** finished
  averages and the period in progress across deep sleep in RTC memory,
  dropping the oldest when full. A power loss or reset empties them.
- **FR-23.10** [Must] [derived from §21] While the device sleeps the sensor
  shall be in its sleep mode (forced mode returns to it after each measurement).
- **NFR-23.1** [Must] [user] A reading, from the forced-mode trigger to the
  compensated values, shall complete within **50 ms** (the datasheet's maximum
  at ×1 is 9.3 ms).
- **NFR-23.2** [Must] [derived from the BME280 datasheet] Compensation shall be
  the datasheet's integer formulas (32-bit temperature and humidity, 64-bit
  pressure), giving 0.01 °C, 1/1024 % and 1/256 Pa resolution before rounding.
- **C-23.1** [Must] [user] The I2C bus shall run at **100 kHz** with the
  ESP32-S3's internal pull-ups enabled in addition to the breakout's.

### 23.4 Verification contracts

| ID | Precondition · stimulus | Expected observation | Must NOT happen | Tier |
|----|------------------------|----------------------|-----------------|------|
| NFR-23.2 (core) | `bme280` compensation core with the datasheet's sample calibration and raw values; minimum and maximum raw values | The datasheet's results (e.g. 25.08 °C, 1006.53 mbar) within one LSB; results clamped to the sensor ranges | Overflow; a result outside −40 – 85 °C, 0 – 100 %, 300 – 1100 mbar | host |
| FR-23.1 (core) | Register writes the driver issues for one reading | `ctrl_hum` 0x01 and `config` 0x00 in sleep mode, then `ctrl_meas` 0x25 (×1, ×1, forced) | Normal mode; any oversampling other than ×1 | host |
| FR-23.4 (core) | Driver on a fake bus: no ACK; chip ID 0x58 (BMP280); ID 0x60 | `ESP_ERR_TIMEOUT` or a bus error; `ESP_ERR_NOT_FOUND`; `ESP_OK` | A reading kept on any failure | host |
| FR-23.9 (core) | History core: add 0, 1, 144, 145 readings | 0, 1, 144, 144 kept, newest first; the 145th drops the oldest | More than 144; a dropped newest | host |
| FR-23.12 (core) | Line 4 for 21.43 °C 45.2 % 1013.25 mbar; −40.0 °C 100 % 1100 mbar; 85 °C; none kept | `70.6F Hum 45% 1013.25 mbar`; `-40.0F Hum 100% 1100.00 mbar`; `185.0F …`; `Sensor --` | More than 33 characters | host |
| FR-23.18 (core) | Render the JSON with 0, 1 and 144 readings, synced and unsynced, the loop off and on | Valid JSON, exactly the six keys per element, newest first; `ext` `{}` or one key | Extra or missing key; invalid JSON; `NaN` | host |
| FR-23.3, FR-23.5, NFR-23.1 | Reset over USB with the sensor fitted | L21 before the window opens, values within 14 – 113 °F, 5 – 95 %, 870 – 1085 mbar | L22; no L21 | target |
| FR-23.17, FR-28.2 (core) | Form bodies: no fields; 15 / 60; 5 / 30; 60 / 1440; 0; 61; 15 / 50; 20 / 30; 60 / 60 | 10 / 60; valid as given; 400 naming `sample_min` (0, 61) or `average_min` (50, 30, 60) | A sample period not dividing the average; an average not dividing a day | host |
| FR-23.15, FR-23.16 (core) | Averages core: readings in one hour, then one in the next; a gap of hours; 98 hours; period starts at UTC −4 | One average, the mean of the hour's readings, when the next hour's first reading comes; no average for the empty hours; 96 kept, newest first; periods on the local hour and local midnight | An average of a period still running; an average for a period with no readings; more than 96 | host |
| FR-23.17, FR-23.9, FR-23.5 | Sleep test build with the period cut to 10 s, three timer wakes, then a key wake | An L21 on every timer wake; `/api/readings` lists them, newest first | A timer wake without L21; WiFi or panel on a timer wake | target / bench (operator) |
| FR-23.4 | Fault build reading address 0x77 | L22 with `ESP_ERR_TIMEOUT`, then the window, ONLINE and `web: sleep` as usual | A restart; a reading kept; no sleep | target |
| FR-23.12 | ONLINE with the sensor fitted | Operator confirms line 4 matches the latest L21 | Line 4 missing, clipped or stale | bench (operator) |
| FR-23.7, FR-23.18 | ONLINE; relay `GET /` and `GET /api/readings` | Page shows the newest reading's values; JSON parses as §23.2 and its first element equals the latest L21 | 404; a value differing from L21 | bench |
| FR-23.1 | ONLINE; operator holds a finger on the sensor for 30 s, then presses the user key | The next L21 shows a higher temperature than the one before | No change | bench (operator) |

## 24. Setup Check

### 24.1 Purpose & peer

When WiFi has just been configured, the device does not drop straight into its
sleep cycle. It holds with the panel lit, shows live readings and asks the
installer to press a key to continue, so the sensors can be checked in place
first, and, when `gps` is `on`, the Notecard takes a GPS fix of the place
(§33.5, FR-33.15). A pending mark in NVS says the check is
still owed; it survives sleep, reset and power loss. The logic layer (§5)
decides when to hold and when the hold ends; the composition root reads the key
and the clock. Either key ends the hold: the BOOT key, GPIO 0, next to the LED
that blinks during the hold, or the user key, GPIO 21 (§21.1), the key that
wakes the device. Both are active low. Held for 5 s, BOOT still requests a
factory reset (§18).

### 24.2 Requirements

- **FR-24.1** [Must] [user] When the portal stores credentials (T3), the device
  shall mark the setup check pending in NVS. A factory reset (T10) clears the
  mark with the credentials; credentials stored by an earlier firmware carry no
  mark.
- **FR-24.2** [Must] [user] While the mark is pending, the device shall hold,
  from T3 and on every later boot that is not a timer wake: the task does not
  run and the device does not sleep, whatever the WiFi state. Panel, WiFi, web
  server and OTA run as in the window; the 60 s window of FR-21.2 starts only
  when the hold ends by key.
- **FR-24.3** [Must] [user] During the hold, line 4 shall show a live reading,
  taken every **5 s**; these readings are neither kept
  (FR-23.9) nor logged as L21.
- **FR-24.13** [Must] [user] During the hold, line 5 shall show
  `Press flashing LED to start <mm>:<ss>`, counting down once a second from
  `30:00` to `00:00` (33 characters); the flashing LED of FR-24.9 marks the
  BOOT key beside it.
- **FR-24.9** [Must] [user] During the hold, the LED next to the BOOT key
  (GPIO 38 on the non-touch board) shall flash at **1 Hz**, 500 ms on and
  500 ms off; when the hold ends it is lit again with the panel, as outside
  the hold.
- **FR-24.11** [Must] [user] A press of the BOOT key or the user key during
  the hold, debounced **50 ms**, shall end it: the mark is cleared, L24 `by=key` is
  logged, line 5 is emptied, and the 60 s window runs, then the task and sleep
  as usual (§21). A BOOT press held on for 5 s also raises the factory
  reset at its release (FR-18.3).
- **FR-24.6** [Must] [user] If no press comes within **30 min** of the hold
  starting, the hold shall end with L24 `by=timeout`; the device runs the task
  and sleeps at once, and the mark stays pending, so the next key wake or
  power-on holds again. A reset or power loss during the hold starts the
  countdown again from `30:00`.
- **FR-24.7** [Must] [derived from FR-21.13] A timer wake shall never hold,
  whatever the mark.
- **FR-24.8** [Must] [derived] Entry into the hold shall log L23 (§7.2).

### 24.3 Verification contracts

| ID | Precondition · stimulus | Expected observation | Must NOT happen | Tier |
|----|------------------------|----------------------|-----------------|------|
| FR-24.1, FR-24.2, FR-24.7 (core) | Logic: T3; boot (power-on, reset, key wake, timer wake) with the mark pending and not | Hold after T3 and after every pending non-timer boot; no hold on a timer wake or with no mark | The task due during a hold; a hold on a timer wake | host |
| FR-24.13 (core) | Countdown line for 1800, 61, 0 s left | `Press flashing LED to start 30:00`; `… 01:01`; `… 00:00` | More than 33 characters | host |
| FR-24.11, FR-24.6 (core) | Hold, then a key event; hold, then 1800 s with no key | Mark cleared, window 60 s, then the task; mark kept, the task at once | Sleep inside the hold; the mark cleared by the timeout | host |
| FR-24.1, FR-24.2, FR-24.8 | Erased NVS; provision through the portal (bench AP) | L4, then L23; no `web: sleep` for 120 s | Sleep or the task during the hold | target |
| FR-24.11 | Holding; operator presses BOOT briefly; again after power-on with the mark pending, the user key | L24 `by=key` each time, then `web: sleep` 60 – 63 s later; no L15 | Sleep earlier; no L24; a factory reset | bench (operator) |
| FR-24.6 | Sleep test build (hold timeout cut to 60 s), provisioned | L24 `by=timeout` 60 – 63 s after L23, then L21 and `web: sleep`; after a key wake, L23 again | Sleep before the timeout; no hold after the key wake | target / bench (operator) |
| FR-24.9 (core) | LED state 0, 499, 500, 999, 1000 ms into the hold; outside the hold | Lit, lit, dark, dark, lit; lit | Dark outside the hold; a period other than 1 s | host |
| FR-24.9 | Holding; operator watches the LED next to the BOOT key for 10 s, then presses BOOT | About ten flashes, then steadily lit after the press | No flashing; the LED dark after the press | bench (operator) |
| FR-24.3, FR-24.13 | Holding; operator watches the panel for 15 s, finger on the sensor | Line 5 counts down once a second; line 4 changes at least once | A frozen countdown; line 4 unchanged | bench (operator) |

---

## 25. Summary Email

### 25.1 Purpose & peer

At a time and interval the user sets, the device emails one plain-text summary
of the readings it kept since the last summary to one recipient, or as two
separate emails to two. It sends
directly to the user's SMTP server (Gmail or the user's own, with a public
certificate) over TLS; no relay service is involved. The mail settings are
entered in the provisioning portal next to the WiFi credentials and kept in
NVS. Peer: the SMTP server named in the settings, reached through the stored
WiFi network. Built on a shared `mail_tx` component (pure core for the SMTP
dialogue and the message text, thin TLS transport); the logic layer decides
when a summary is due and what it says, the composition root sends it.

A timer wake normally keeps WiFi off (FR-21.13); when a summary is due on a
timer wake, the device brings WiFi up only long enough to send it.

### 25.2 Portal fields

Added to the §13.2 form; all optional as a group, so a submission with only
`ssid` and `password` (the testbench's `enter-portal` call) stays valid and
leaves email off.

| Field | Format | Valid |
|-------|--------|-------|
| `mail_host` | SMTP server host name, e.g. `smtp.gmail.com` | 1 – 64 bytes, letters, digits, `.` and `-` |
| `mail_port` | `465` (TLS from the start) or `587` (STARTTLS) | exactly one of the two |
| `mail_user` | login name | 1 – 64 bytes |
| `mail_password` | for Gmail an app password | 1 – 64 bytes |
| `mail_from` | sender address, if not the login; one the server lets this login send as | empty, or 3 – 64 bytes, one `@` with text on both sides |
| `mail_to` | recipient address | 3 – 64 bytes, one `@` with text on both sides |
| `mail_to2` | second recipient address | empty, or as `mail_to` |
| `mail_time` | first summary, local time | `hh:mm`, 00:00 – 23:59 |
| `mail_every` | interval between summaries | whole hours, 1 – 24 |

### 25.3 Requirements

- **FR-25.1** [Must] [user] The portal form (§13.2) shall carry the fields of
  §25.2. A submission with all of them empty or absent shall be valid and leave
  email off; a submission with any of them filled shall be valid only if every
  one is valid per §25.2, else answer 400 naming the first invalid field.
- **FR-25.16** [Must] [user] Valid mail settings shall be stored in NVS with the
  WiFi credentials at T3, or by an accepted BLE write in a key window (T14,
  FR-26.7), and erased by a factory reset (T10). Settings change only by a
  factory reset and a new provisioning, or by such a BLE write.
- **FR-25.3** [Must] [user] A summary is due at `mail_time` local time (FR-22.2)
  and every `mail_every` hours after it, counted from `mail_time` each day. The
  task (§21) shall send one summary on its first run at or after a due time;
  due times missed in between (the device was off, or a send failed) are
  skipped, never sent late in a batch.
- **FR-25.12** [Must] [user] The summary shall be one plain-text email from
  the sender (FR-25.13) to `mail_to` and, when `mail_to2` is given, a separate
  email with the same text to `mail_to2`, with the subject `<hostname> summary
  <YYYY-MM-DD> <hh:mm>` (local time of sending) and a body giving: the device
  name (FR-20.1), the period covered (time of the oldest and newest reading
  included), the number of readings, the newest reading in the format of
  FR-23.12, and the minimum and maximum of temperature (°F), humidity (%) and
  pressure (mbar) over every reading taken since the previous summary was
  sent, or since the boot for the first summary after a power loss or reset.
- **FR-25.15** [Must] [user] After that summary, the body shall hold a blank
  line, the header line `"Time","Temp F","Hum %","ATM mbar"`, and one line per
  average (FR-23.15) finished since the previous summary was sent, oldest
  first: `"<YYYY-MM-DD hh:mm>","<t>","<rh>","<p>"`, the period's local start
  time, temperature in °F with 1 decimal, humidity a whole number, pressure in
  mbar with 2 decimals, no units. When averages were dropped (FR-23.16) before
  they could be sent, the summary block first says `Averages not kept: <n>
  earlier periods`. With no finished average there is no table.
- **FR-25.13** [Must] [user] The sender, both the SMTP envelope (`MAIL FROM`)
  and the `From:` header, shall be `mail_from` when given, else `mail_user`;
  the login is always `mail_user`.
- **FR-25.5** [Must] [user] The device shall connect to `mail_host` on
  `mail_port`: on 465 with TLS from the first byte, on 587 by plain SMTP then
  STARTTLS; it shall verify the server certificate against the ESP-IDF CA
  bundle and the host name, then authenticate with `mail_user` and
  `mail_password` (AUTH LOGIN or AUTH PLAIN, whichever the server offers).
- **FR-25.6** [Must] [user] When a summary is due on a timer wake, the task
  shall start WiFi as a station with the stored credentials, send the summary,
  stop WiFi (FR-21.7) and sleep; the panel stays dark. On a timer wake with no
  summary and no ThingsBoard upload due, WiFi stays off (FR-21.13).
- **FR-25.14** [Must] [user] An email that cannot be sent (no WiFi within
  **15 s**, a DNS, TCP, TLS or certificate failure, a server refusal, or no
  final `250` within **30 s** of the summary's first connection, a budget both
  recipients share) shall be dropped and logged as L26 with the reason and the
  recipient; it shall not be retried, the other recipient's email is still
  tried, and the device goes on to sleep as usual.
- **FR-25.8** [Must] [derived from FR-25.3 and FR-22.6] With the clock never
  set (FR-22.6), no summary shall be due; the task logs nothing for it.
- **FR-25.9** [Must] [derived] Each summary sent shall be logged as L25, each
  dropped one as L26 (§7.2).
- **FR-25.10** [Must] [derived from §9.1] The device shall not output
  `mail_password` on serial, UDP, any HTTP response or in any email (FR-9.3).
- **FR-25.11** [Must] [derived from FR-25.3 and FR-25.9] A send that a reset
  or power loss cuts short, before L25 or L26 is logged, shall be tried once
  more on the next task run; if that try is cut short too, the summary is
  dropped.
- **NFR-25.1** [Must] [derived from FR-25.14] A timer wake that sends a summary
  shall be back asleep within **50 s** of the wake, success or failure.

### 25.4 Verification contracts

| ID | Precondition · stimulus | Expected observation | Must NOT happen | Tier |
|----|------------------------|----------------------|-----------------|------|
| FR-25.1 (core) | Form bodies: WiFi only; all mail fields valid; each field invalid in turn; port 25; `mail_to` without `@`; `mail_every` 0 and 25 | Valid, email off; valid, settings parsed; 400 naming that field each time | A partial mail group accepted | host |
| FR-25.3, FR-25.8 (core) | Due check for `mail_time` 07:00 and `mail_every` 6 at 06:59, 07:00, 12:59, 13:05, after a send at 07:02 the same day, after two missed slots, with the clock never set | not due, due, not due, due, not due until 13:00, one due (no batch), never due | Two summaries for one slot; a summary with the clock unset | host |
| FR-25.12, FR-25.10 (core) | Body for 0, 1 and 144 readings | Subject and body as FR-25.12; min/max correct; no password in the text | The password anywhere in the message | host |
| FR-25.5 (core) | SMTP core against scripted server replies for 465 and 587, AUTH LOGIN and PLAIN, a 535 refusal, a reply split across reads | The commands of RFC 5321 in order, dot-stuffed body, success only on the final 250; the 535 reported | Data sent before 220/EHLO; success on a refusal | host |
| FR-25.6, FR-25.9, NFR-25.1 | Sleep test build with a mail slot due on the next timer wake, test server reachable | `web: wake cause=timer`, L25, `web: sleep` within 50 s; panel dark | WiFi on a timer wake with no slot due; L2 or L7 lines | target / bench (operator) |
| FR-25.11 (core) | Due slot with no send begun; the same slot after a send begun and cut short | First try; last try (handled before the send) | A third try; the slot handled before the first try | host |
| FR-25.11 | Provisioned, a slot due on the next timer wake; the testbench resets the chip about 3 s into the wake | No L25 or L26 from the cut-short wake; L25 at the end of the next boot's window | No summary at all; two summaries | bench |
| FR-25.15 (core) | Body after 2 finished hours (two readings in the first); 1 of 4 since the last summary; none finished; 99 since, 3 dropped; 96 at the sensor's limits | Header and two lines, the means of each hour, oldest first; only the newest; no table; the note and 96 lines; every line fits the body | The running period listed; units in the table; the body cut short | host |
| FR-25.12, FR-25.13 (core) | Form bodies with `mail_from` and `mail_to2` empty, absent, valid and invalid | Sender the login unless `mail_from` is given; one or two recipients; 400 naming the invalid field | `mail_from` used as the login; a second recipient from an empty field | host |
| FR-25.14 | Same build, `mail_host` unreachable | L26 with the reason, then `web: sleep` within 50 s | A retry; no sleep | target |
| FR-25.12 | Operator's own Gmail account, provisioned through the portal | The summary arrives as FR-25.12 | A missing field; mbar not stated | bench (operator) |
| FR-25.12, FR-25.13 | Operator's own provider, provisioned with `mail_from` an identity of the login and `mail_to2` a second address | Two emails arrive, one per address, each showing `mail_from` as the sender; L25 twice | One email to both; the login shown as the sender | bench (operator) |

---

## 26. BLE Setup

### 26.1 Purpose & peer

Lets a person set up or change the device's settings from a phone or computer
over Bluetooth Low Energy, as an alternative to the SoftAP portal (§13), which
stays. The same settings travel in the same form body the portal accepts
(§13.2, §25.2) and are checked by the same rules. Peer: one BLE central at a
time (the project's Web Bluetooth page, nRF Connect, or the testbench), paired
with a passkey the device shows on its panel. Built on the shared `ble_setup`
component (NimBLE acting only as a peripheral, with its GATT client built in
to confirm the indications a central sends); the logic layer (§5) decides when
BLE runs, the composition root starts and stops it.

BLE runs only inside a **window**:

| Window | Opens | Closes |
|--------|-------|--------|
| **Provisioning** | Entry into PROVISIONING (T1), with the SoftAP | T3 (from either the portal or BLE), or a restart |
| **Key** | The user key (GPIO 21) held 3 s while the device is awake, in any connectivity state but PROVISIONING | 5 min with no central connected, a `control` write of `done`, an accepted settings write (T14), or a restart |

### 26.2 GATT service

One primary service, UUID `6e0a0001-7d3b-4c2a-9f1e-54445333424c`
(`T D S 3 B L` in the last six bytes), carried in the advertising data, with
the complete local name, the SoftAP SSID of FR-13.1 (`TDS3-<XXXXXX>`), in the
scan response (both do not fit one 31-byte advertisement). Every value is UTF-8
text in the `application/x-www-form-urlencoded` format of §13.2.

ATT limits one attribute value to 512 bytes, so a body is **staged**: each
`settings` write (up to 512 bytes) appends to it, a `control` write of `apply`
validates and acts on the whole staged body, and the staged body is emptied
after `apply` and on every new connection. A `settings` read returns 512 bytes
at most, starting at the offset last set by `control` `read=<offset>` (0 after
connecting).

| Characteristic | UUID | Access | Link required | Value |
|----------------|------|--------|---------------|-------|
| `info` | `6e0a0002-…` | read | none | `fw=<V>&host=<hostname>&state=<S>` |
| `settings` | `6e0a0003-…` | read, write | encrypted, authenticated (MITM) | Read: a page of the stored settings of Appendix A that are not secret, then `password_set=<0\|1>&mail_password_set=<0\|1>&tb_token_set=<0\|1>`; after `control` `form=<offset>`, a page of the form description of FR-26.19 instead. Write: appended to the staged body |
| `result` | `6e0a0004-…` | read, notify | encrypted, authenticated (MITM) | `none` after connecting; after each `apply`, `ok` or `invalid=<field>` naming the first invalid field |
| `control` | `6e0a0005-…` | write | encrypted, authenticated (MITM) | `apply` submits the staged body (FR-26.6, FR-26.7); `done` closes a key window; `read=<offset>` sets the read page; `form=<offset>` sets the read page of the form description (FR-26.19); `forget` erases every bond (FR-26.13); `time=<ms since 1970 UTC>` carries the phone's clock (FR-39.5); any other value is answered with the ATT error *Value Not Allowed* |

(`…` is the rest of the service UUID.)

### 26.3 Requirements

Windows:

- **FR-26.1** [Must] [user] On entering PROVISIONING the device shall start BLE
  advertising, alongside the SoftAP and portal (§13), and stop it at T3.
- **FR-26.2** [Must] [user] While the device is awake and not in PROVISIONING,
  holding the user key (GPIO 21) continuously for **3 s** shall open a key
  window: BLE advertising starts within 1 s of the 3 s mark. A press that ends
  the setup-check hold (FR-24.11) and is held on to 3 s also opens a window.
- **FR-26.3** [Must] [user] A key window shall close **5 min** after it opened
  if no central has connected, and 5 min after the last central disconnected
  otherwise; a `control` write of `done` shall close it at once. Closing stops
  advertising, drops any connection, and releases the BLE controller and host.
- **FR-26.4** [Must] [user] BLE shall never run on a timer wake, and never
  while the display is in LCD_FAILED or its start failed: a key hold then
  opens no window.
- **FR-26.5** [Must] [derived from FR-26.3 and §21] While a key window is open
  the task shall not run and the device shall not sleep; the 60 s window of
  FR-21.2 runs again from the moment the key window closes.

Settings:

- **FR-26.6** [Must] [user] In PROVISIONING, an `apply` of the staged
  body shall be a credentials submission (T3, T4) with the validation of FR-13.4 and FR-25.1:
  valid, it is stored as a portal submission is (FR-5.13, FR-24.1, FR-25.16) and
  `result` becomes `ok`; invalid, NVS is unchanged and `result` becomes
  `invalid=<field>`.
- **FR-26.7** [Must] [user] In a key window, an `apply` of the staged body shall replace
  the stored settings, validated as in FR-26.6, except that a write without an
  `ssid` field keeps the stored WiFi credentials, a write with `ssid` but
  without a `password` field keeps the stored WiFi password, and a write with
  mail settings but without a `mail_password` field keeps the stored mail
  password. Valid, the settings are stored, `result` becomes
  `ok` and the device restarts (T14); invalid, NVS is unchanged and `result`
  becomes `invalid=<field>`.
- **FR-26.8** [Must] [derived from FR-26.7] After an accepted key-window
  write the device shall notify `result`, then restart within **2 s**
 ; the setup-check mark is not set by this write
 .
- **FR-26.9** [Must] [derived from §9.1, FR-9.3, FR-25.10, FR-32.3] No BLE
  read or notification shall carry `password`, `mail_password` or `tb_token`;
  `settings` reads report only whether each is stored (`password_set`,
  `mail_password_set`, `tb_token_set`).
- **FR-26.10** [Must] [derived from `PORTAL_BODY_MAX`] A `settings` write that
  would make the staged body longer than **2048** bytes shall be refused with
  the ATT error *Invalid Attribute Value Length*, empty the staged body, and
  leave NVS and `result` unchanged.

Security:

- **FR-26.11** [Must] [user] The device shall pair only by LE Secure
  Connections with passkey entry, declaring the IO capability *DisplayOnly*:
  a fresh random 6-digit passkey is shown on line 5 (FR-26.23) and the central
  enters it.
- **FR-26.12** [Must] [derived from FR-26.11] `settings`, `result` and
  `control` shall answer a central whose link is not encrypted with an
  authenticated (MITM) key with the ATT error *Insufficient Authentication*;
  only `info` is readable before pairing.
- **FR-26.13** [Must] [user] The device shall bond with a central that pairs
  and keep at most **3** bonds in NVS, so a bonded central reconnects in a
  window by encryption alone, without a new passkey; a factory reset (T10)
  and a `forget` written to `control` (the page's *Forget all phones*) erase
  them. A central that starts encryption with a key the device does not
  hold is disconnected at once (FR-26.22). After
  **3** failed pairings in one window it shall close a key window, or pause
  advertising for **60 s** in PROVISIONING.
- **FR-26.15** [Must] [derived from FR-26.11] At most one central shall be
  connected at a time; while one is connected the device does not advertise.
- **FR-26.22** [Must] [proposed, from the user's report that pairing failed
  after an erase] The device shall advertise at a static random address kept
  in NVS, used again while it holds at least one bond and replaced by a new
  random one at a window's start whenever it holds none (an erased flash, a
  factory reset, a `forget`). A phone that kept its half of a pairing the
  device no longer holds then meets a new device and pairs afresh, without the
  device being removed from the phone first.

Display and log:

- **FR-26.23** [Must] [proposed, from the user's report that the Setup WiFi
  screen gave no sign of BLE] While a passkey is awaited, line 5 shall show
  `BLE code <nnnnnn>`; otherwise `BLE connected` while a central is
  connected, and with none, in a key window `BLE setup <mm>:<ss>` counting
  down the time left of FR-26.3, and in the provisioning window
  `BLE setup on`.
- **FR-26.16** [Must] [derived] Window openings and closings, pairing outcomes
  and settings writes shall log L27 – L30 (§7.2); the passkey shall not be
  logged on serial or UDP.

Coexistence:

- **FR-26.17** [Must] [user] A `POST /ota` while a key window is open
  shall be answered 409 (FR-15.12) and start no download, and a key hold during
  an OTA download shall open no window.
- **FR-26.18** [Must] [derived from §9.1 and FR-26.3] The firmware shall
  initialise the BLE controller only when a window opens, and release it when
  the window closes; with no window open, nothing advertises at the ESP32-S3's
  derived MAC addresses (base MAC + 0 … + 3) or at the address of FR-26.22.
- **NFR-26.1** [Must] [user] 10 s after a window closes, `free_heap` shall
  be within **8 KB** of its value before the window opened.

The setup page:

- **FR-26.19** [Must] [user] The device shall describe the rules of every
  field a `settings` read shows, the same rules as FR-13.8, in a form
  description read through `settings` after `control` `form=<offset>`: a
  form body with one `<name>=<rules>` per field (a secret by its name), where
  `<rules>` is itself a form body of `label`, `required`, `group` and `need`
  (required once any field of the group is filled), `required_if`
  (`<name>=<value>`: required while that field holds that value, FR-34.3), `type`, `minlength`,
  `maxlength`, `min`, `max`, `step`, `pattern`, `options` (a leading empty item
  allows none) with their `labels`, `extras` (checkboxes added to a choice) with
  `extra_labels` and `extras_intro`, `placeholder`, `inputmode`, `unit`,
  `section`, `intro` and `hint`. The page shall
  check every field against them before it writes, mark required fields with
  `*`, and write nothing while a field fails. A stored secret left empty is
  never required (it keeps the stored one, FR-26.7).
- **FR-26.20** [Must] [user] When `result` is `invalid=<field>`, the page shall
  keep every value typed and name the field by its label, marked until it is
  changed; values typed and not saved shall also be kept when the page
  reconnects to the same device and draws the form again.
- **FR-26.21** [Must] [user] The page shall draw the portal form of §13.2
  (with §25.2, §28.2, §29.4 and §31.2): the same fields in the same order, under
  the same section headings with the same intro text, with the same labels
  (a second box the portal puts on the same line named in full, e.g.
  *Temperature above*), choices and their texts (the setup and its tests as a
  choice and checkboxes), units, placeholders and checks, and the same
  suggested ntfy topic (FR-31.3) while none is stored.

### 26.4 Verification contracts

| ID | Precondition · stimulus | Expected observation | Must NOT happen | Tier |
|----|------------------------|----------------------|-----------------|------|
| FR-26.1 | Erased NVS; bench BLE scan 10 s after L3 | Advertiser `TDS3-<XXXXXX>` carrying the service UUID; L27 `reason=provisioning` | No advertiser; another name | bench |
| FR-26.1, FR-26.6, FR-26.11 | PROVISIONING; bench pairs (passkey read by operator from line 5), writes `ssid`/`password` of the bench AP to `settings`, then `apply` to `control` | `result` = `ok`; L29 `ok`, L4, K4, L30 `reason=provisioned`; ONLINE on the bench AP | NVS written before the passkey; the SoftAP still up 5 s after the answer | bench (operator) |
| FR-26.6 (core) | Form bodies as FR-13.4 and FR-25.1 cores | Same verdicts and field names as the portal | A body the portal refuses accepted, or the reverse | host |
| FR-26.7 (core) | Key-window writes: no `ssid`; `ssid` without `password`; mail fields without `mail_password`; email turned off; each invalid field | Kept credentials; kept WiFi password; kept mail password; email off; `invalid=<field>` | A secret erased by an absent field; a partial write stored | host |
| FR-26.2, FR-26.3 | ONLINE; operator holds the user key 3 s; bench scans every 10 s for 6 min, no connection | L27 `reason=key` ≤ 1 s after the 3 s mark; advertiser seen; L30 `reason=idle` 300 – 303 s after L27; no advertiser afterwards | Advertising past the 5 min; a window from a 2 s hold | bench (operator) |
| FR-26.3 (core) | Window rule fed: open at 0 s, no connection to 300 s; connect at 100 s, disconnect at 250 s; `done` at 10 s | Close at 300 s; close at 550 s; close at 10 s | A close while connected; a close before the 5 min | host |
| FR-26.4 (core) | Window request on a timer wake; in LCD_FAILED; at LCD start failure | No window each time | A window | host |
| FR-26.5 (core) | Key window open from 50 s to 400 s after a power-on boot | No task, no sleep before 460 s | The task inside the window | host |
| FR-26.7, FR-26.8 | ONLINE, key window, paired; stage the stored values read from `settings` with `mail_every=6`, then `apply` | `result` = `ok`, L29 `ok`, L30 `reason=written`, L1 within 2 s (+3 s tolerance); after boot `settings` reads `mail_every=6`; no L23 | A restart before the notification; the WiFi password changed | bench (operator) |
| FR-26.9 | Provisioned with password `pw-<random 12 hex>` and a mail password marker; read `settings`; capture serial and UDP through a window | `password_set=1`, `mail_password_set=1`; neither marker anywhere | Either marker in a read, a notification or a log line | bench (operator) |
| FR-26.9 (core) | `dump` of settings holding both passwords | Only the `_set` flags | Any password byte | host |
| FR-26.10 (core) | Staged writes of 4 × 512 bytes, then 1 more byte | Kept; refused and the staged body emptied | A body over 2048 bytes kept | host |
| FR-26.12 | Window open; bench connects without pairing and reads `settings` | ATT error Insufficient Authentication; `info` readable | A settings value | bench |
| FR-26.13 | Key window; bench fails the passkey 3 times | L28 `failed` three times, then L30 `reason=pairing` | A fourth attempt accepted | bench (operator) |
| FR-26.23 (core) | Line 5 for a passkey 4207, 300 s and 61 s left, connected; provisioning window, unconnected and connected | `BLE code 004207`, `BLE setup 05:00`, `BLE setup 01:01`, `BLE connected`; `BLE setup on`, `BLE connected` | More than 33 characters | host |
| FR-26.16 (core) | Format L27 – L30 with sample values | The §7.2 strings exactly | A passkey in any line | host |
| FR-26.17 | Key window open; relay `POST /ota` | 409, no download, the window stays open | A download | bench (operator) |
| NFR-26.1 | ONLINE; `free_heap` from `/api/status`; open and close a key window | Within 8 KB 10 s after L30 | A larger loss | bench (operator) |
| FR-26.4 | Sleep test build, timer wake; bench scans through it | No advertiser | An advertiser on a timer wake | target |
| FR-26.18 | ONLINE, no window; bench BLE scan 30 s | No advertiser at base MAC + 0 … + 3 or at the FR-26.22 address seen in the last window | Such an advertiser | bench |
| FR-26.22 (core) | Random bytes through the address rule; the keep rule with and without a stored address and bonds | A static random address (top two bits set), all-0 and all-1 refused; kept only with a stored address and a bond | The public address; an address kept with no bond | host |
| FR-26.22 | Phone paired in a key window; factory reset (T10); provision again over BLE from the same phone without removing the device from it | The page pairs with a new passkey on its first try; L28 `ok` | A stale-key disconnect; the page asking to remove the device | bench (operator) |
| FR-26.19 (core) | Read the form description; parse it and the `settings` dump | One rule set per field shown, each with a label; `ssid` required ≤ 32; `password` 8 – 63; every mail field in group `mail`, the seven of FR-25.1 `need`; port options none,465,587; `setup` options std,fine,slow,custom with extras quick,cycle; the `mail_host` and loop patterns accept and refuse what the device does | A field without rules; a rule looser or stricter than §13, §25.2, §28 | host |
| FR-26.21 (core) | The form description against the portal form HTML in the sources | Same fields in order, sections, intro text, labels, option values and texts, extras, units and checks | Any difference but the named full labels of second boxes | host |
| FR-26.19, FR-26.21 (page) | The page in a headless browser against a simulated device | Headings, intros, units, labelled choices and checkboxes drawn; the choice and ticked extras written as one value; a 2-decimal pressure limit written; nothing written while a field fails | A decimal limit refused by the browser; an empty choice not in the rules | host |
| FR-26.19, FR-26.20 | Key window, paired from the setup page: fill only `mail_host`; save; empty it; type a 7-character WiFi password; save; then `setup` custom, `average_min` 7; save; disconnect and reconnect; `average_min` 60; save | Nothing written the first two times; `result` `invalid=average_min`, the page names *Custom: average them over* with every value kept, and still after the reconnect; then `ok` | A write with a partial mail group; a value lost | bench (operator) |
| FR-26.18 (core) | Transition function over every §5.3 row and window event | BLE start only on a window opening; BLE stop on every closing and before every restart | A start without a window; a window left open across T10, T11, T13 or T14 | host |

---

# Part E — Operations & Verification

## 27. Reading History

### 27.1 Purpose & peer

Keeps every reading (§23) in flash, so the history survives a power loss or
reset and covers more than a year rather than the 24 h kept in RTC memory (FR-23.9). The
history can be downloaded as CSV over WiFi (`GET /api/history`) or over BLE in a
key window (§26). Built on the shared `flash_ring` component (fixed-size
records in a data partition, the oldest sector reused when full, a torn record
skipped); the composition root appends and reads, a pure core formats.

A record holds one reading: the time (seconds since the epoch, 0 for a clock
never set, FR-22.6), the synced mark (FR-22.3), temperature in 0.01 °C,
humidity in 1/1024 % and pressure in 1/256 Pa, the sensor's own units, so a
history line shows the values of its L21, and the two add-on values of
§29.2 as 32-bit floats (NaN for none). It takes 24 bytes, so a record with
the ring's trailer takes 32 bytes and a 4 KB sector holds 128.

A history line is CSV, oldest first, after the header
`time,synced,temp_f,rh_pct,p_mbar`, followed by one column per enabled
add-on named by its setting (§29.2), e.g. `,tank` with the loop on:

| Column | Format | Example |
|--------|--------|---------|
| `time` | `<YYYY-MM-DD>T<hh:mm:ss><±hhmm>`, local time (FR-22.2); empty for a clock never set | `2026-11-01T12:31:07-0500` |
| `synced` | `0` or `1` | `1` |
| `temp_f` | °F, 2 decimals | `70.62` |
| `rh_pct` | %, 1 decimal | `45.2` |
| `p_mbar` | mbar, 2 decimals | `1013.25` |

### 27.2 Requirements

- **FR-27.1** [Must] [user] Each kept reading (FR-23.1, FR-23.3) shall also be
  appended to the history, on every wake including timer wakes.
- **FR-27.11** [Must] [user] The history shall live in the `history` partition
  of C-10.10: **2 MB**, about **65,000 readings**, 15 months at the
  default 10-minute `sample_min`. When it is full the oldest sector's
  readings are dropped.
- **FR-27.3** [Must] [derived from `flash_ring`] A reading cut short by a
  power loss or reset shall be skipped on the next read, and the history
  otherwise kept; it never stops the device starting.
- **FR-27.4** [Must] [user] In ONLINE, `GET /api/history` shall answer 200
  `text/csv` with the header and every history line, oldest first, sent in
  chunks so the response never needs the whole history in RAM; only the header
  when the history is empty. `GET /` shall link to it.
- **FR-27.10** [Must] [user] `GET /api/history?from=<YYYY-MM-DD>&to=<YYYY-MM-DD>`
  shall send only the lines whose local time (FR-22.2) falls on or after the
  `from` date and on or before the `to` date, both whole days, oldest first,
  still with the header; either may be left out for an open end. With either
  given, lines with a clock never set are left out. A date that is not valid,
  or `from` after `to`, answers 400 with a one-line reason. Lines are chosen by their time, not by their place in the ring, so
  a clock set back still gives every matching line.
- **FR-27.5** [Must] [user] In a key window, a paired central shall read the
  history through the `history` characteristic (§27.3), starting at the line
  set by `control` `hist=<n>` (0 is the oldest). After `control`
  `range=<from>,<to>` (dates as in FR-27.10, either empty for an open end),
  the lines and n count only the lines FR-27.10 would send for that range; a
  new connection starts with all lines.
- **FR-27.6** [Must] [user] The Web Bluetooth page shall offer **Download
  history** with optional **From** and **To** dates (empty for an open end),
  read every line through FR-27.5 after setting that range, show progress and save the result
  as `<hostname>-history.csv` with the header of §27.1.
- **FR-27.7** [Must] [user] A factory reset (T10) shall erase the history
  with the settings; a reset cut short finishes the erase on the next boot.
- **FR-27.8** [Must] [derived from OTA (§15)] A device whose partition table
  has no `history` partition (one updated only by OTA, which never rewrites
  the table) shall run as before: log L31 once per boot, answer
  `GET /api/history` with 404, and give empty `history` reads.
- **FR-27.9** [Must] [derived] A history append or read that fails shall be
  logged as L31 with its error; the reading is still kept in RTC memory
  (FR-23.9) and the device carries on.
- **NFR-27.1** [Must] [user] An append, including a sector erase, shall
  take at most **500 ms**.
- **NFR-27.2** [Must] [user] `GET /api/history` of a full history shall
  complete within **30 s** at the bench AP's signal.

### 27.3 BLE history characteristic

Added to the §26.2 service:

| Characteristic | UUID | Access | Link required | Value |
|----------------|------|--------|---------------|-------|
| `history` | `6e0a0006-…` | read | encrypted, authenticated (MITM) | As many whole history lines (no header) as fit in 512 bytes, starting at the line set by `control` `hist=<n>` (0 after connecting); empty past the last line |

`control` also accepts `hist=<n>`, n at most 9 digits, and
`range=<from>,<to>` (FR-27.10; `range=,` for all; either sets n to 0); the
client asks for the next n by adding the lines it received. An invalid date is
refused like any malformed `control` write (§26.2).

When the `history` partition exists, `info` ends with `&history=<the §27.1
header>`; the page offers **Download history** only then (FR-27.8).

### 27.4 Verification contracts

| ID | Precondition · stimulus | Expected observation | Must NOT happen | Tier |
|----|------------------------|----------------------|-----------------|------|
| FR-27.1, FR-27.4 (core) | Format records: synced, unsynced, clock never set, −40 °C, 85 °C | The §27.1 lines, e.g. `2026-11-01T12:31:07-0500,1,70.62,45.2,1013.25`; empty `time` for a never-set clock | Another column order; missing decimals | host |
| FR-27.5 (core) | Page 0, 1, 10 and 65,110 lines from n = 0, n mid-way, n past the end | Whole lines only, ≤ 512 bytes each page, pages joined equal the full CSV body; empty past the end | A split line; a repeated or skipped line | host |
| FR-27.10 (core) | Records spanning three days, one with a clock never set, one after the clock was set back; from = to = the middle day; only from; only to; from after the newest; from after to; `2026-02-30`; `x` | Only that day's lines; open ends honoured; header first each time; the header only; 400 for the last three | A never-set line; a set-back line missed; a line from the day after `to`; 200 for a bad range | host |
| FR-27.11, FR-27.3 (core) | `flash_ring` core with 64 sectors: fill 8,192 records; tear the last record | The oldest sector dropped, newest kept in order; the torn record skipped | A lost newer record; a failed boot | host |
| C-10.10 | Parse the built partition table binary | Exactly the six rows tabled | Any other row, offset or size | host |
| FR-27.1, FR-27.4 | Sleep test build, three timer wakes, then a key wake to ONLINE; relay `GET /api/history` | CSV with the header and at least the three wakes' readings, oldest first, each equal to its L21 | A missing timer-wake reading; JSON | bench |
| FR-27.3 | Reset over USB during the task 5 times; then ONLINE | `GET /api/history` parses; every line equals an L21 | A boot loop; a garbled line | bench |
| FR-27.5, FR-27.6 | Key window; operator pairs the page and presses **Download history** | The saved file equals `GET /api/history` byte for byte | A different line count | bench (operator) |
| FR-27.10, FR-27.6 | As above with **From** and **To** set to today | The saved file equals `GET /api/history?from=<today>&to=<today>` byte for byte | An older line | bench (operator) |
| FR-27.7 | Operator factory reset, then provision | `GET /api/history` is the header only | An old reading | bench (operator) |
| FR-27.8 | Build flashed over the old five-row table (app only) | L31 `history unavailable` once; `GET /api/history` 404; readings and the rest unchanged | A restart; a write outside the app slot | target |
| NFR-27.1 | Time appends across a sector boundary | Each ≤ 500 ms | Longer | target |

## 28. Sampling Setups

### 28.1 Purpose & peer

The reading and average periods are chosen from a fixed list of **setups**
instead of only two numbers, and the same list holds **test setups**: short,
timed runs that check in one go that the sensor, the history, the averages,
the timer wakes and the email all work, then hand over to normal sampling on
their own. One normal setup runs until it is changed; tests chosen with it run
first, once, in table order. Peer: the person setting up the device, through
the portal (§13) or a BLE settings write (§26), or a double press of the user
key. The pure `sampling_core` decides (selection, test progress, verdict, log
and screen text); the composition root reads the sensor, sleeps and sends.

| Id | Kind | Name | Reading every | Average over | Runs for |
|----|------|------|---------------|--------------|----------|
| `std` | normal | Standard | 10 min | 60 min | until changed |
| `fine` | normal | Frequent | 1 min | 15 min | until changed |
| `slow` | normal | Battery saver | 30 min | 180 min | until changed |
| `custom` | normal | Custom | `sample_min` | `average_min` | until changed |
| `quick` | test | Quick check | 10 s, awake | the normal setup's | 12 readings (2 min) |
| `cycle` | test | Sleep cycle check | 30 s, asleep between | 2 min | 20 readings (10 min) |
| `gps` | test | GPS check (§33, FR-33.20) | no readings | — | until a fix, at most 20 min |

During `quick` and `gps` the device stays awake as in the window (panel, WiFi, web
server); during `cycle` every reading after the first is a timer wake
(FR-21.13).

### 28.2 Requirements

Selection:

- **FR-28.1** [Must] [user] The portal form (§13.2) and a BLE `settings`
  write (§26) shall accept `setup` fields, each a comma-separated list of ids
  of §28.1; all `setup` values together shall name exactly one normal id and
  each test id at most once. With no `setup` value, or only empty ones, the
  normal setup is `custom` when `sample_min` or `average_min` is given and not
  empty, else `std`. Any other selection is refused with 400 naming `setup`
  (portal) or `invalid=setup` (BLE).
- **FR-28.2** [Must] [user] `custom` shall take `sample_min` and `average_min`
  with the rules of §23.1; the other normal ids shall ignore both fields.
- **FR-28.3** [Must] [user] A valid selection shall be stored in NVS with the
  other settings (T3, T14) and erased by a factory reset (T10); a device
  whose settings were stored before §28 runs `custom` with its stored
  `sample_min` and `average_min`. The `settings` read (FR-26.9) shall report
  it as `setup=<normal id>[,<test id>…]` (tests still to run), and `GET /`
  shall name the normal setup and any tests still to run.
- **FR-28.4** [Should] [user] While the device is awake, not in PROVISIONING,
  not holding the setup check and running no test, two presses of the user key
  (GPIO 21), each held at least 50 ms and less than **1 s**, the second
  beginning within **1 s** of the first one's release, shall start `quick`
  at once (FR-28.6), with the normal setup after it. A double press shall not
  open a BLE window (FR-26.2 needs a 3 s hold).

Running a test:

- **FR-28.5** [Must] [user] Stored tests shall start, in table order, when the
  task would first run (FR-21.4) after they were stored: at the end of the 60 s
  window after the T14 restart, or after the setup check (§24) and its window
  after T3. When the last test ends, the device shall continue with the normal
  setup.
- **FR-28.6** [Must] [user] `quick` shall keep the device awake, without
  running the task, and take a reading every **10 s**, the first at its start,
  12 in all; `cycle` shall take a reading at its start and on each of 19 timer
  wakes, sleeping **30 s** after each but the last, 20 in all, with averages
  over **2 min** (FR-23.15) while it runs.
- **FR-28.7** [Must] [user] A test reading shall be a reading in every other
  respect: logged as L21, kept (FR-23.9), averaged (FR-23.15), counted for the
  summary (FR-25.12) and appended to the history (FR-27.1).
- **FR-28.8** [Must] [user] A press of the user key during `quick`, or a key
  wake during `cycle`, shall end the running test as failed with `by=key`; a
  reset or power loss during a test shall end it as failed with `by=reset` on
  the next boot. Either way the remaining tests are dropped, and the device
  continues as after the last test (FR-28.11).
- **FR-28.9** [Must] [user] Each test, once started, shall be cleared from the
  stored selection when it ends, so no later boot runs it again.

Result:

- **FR-28.10** [Must] [user] A test shall pass when it was not ended early,
  each of its readings was taken (no L22) and appended to the history (no
  L31), and, for `cycle` with the clock set (FR-22.6), every 2 min period its
  readings moved on from finished an average; otherwise it fails.
- **FR-28.11** [Must] [user] When the last test ends, or one is ended early,
  the device shall continue as after a key wake (FR-21.11): panel, WiFi and web
  server up, and the 60 s window from that moment, then the task with the
  normal setup. During that window line 5 shall show `Test pass <n>/<m>` or
  `Test FAIL <n>/<m>`, n the readings that counted and m those planned for the
  tests that ran, unless the BLE key window shows its own line 5 (FR-26.23).
- **FR-28.12** [Must] [derived] Each test's start shall log L32 and its end
  L33 (§7.2).
- **FR-28.13** [Should] [user] When mail is set up (§25), the device shall
  send one result email to `mail_to` (and separately to `mail_to2`) once it is
  ONLINE in that window, from the sender of FR-25.13, with the subject
  `<hostname> test <pass|FAIL>` and a body of the L33 line of each test that
  ran, after `web: `. It is logged as L25 (`readings=` the n of FR-28.11) or
  L26, and if the device is not ONLINE when the window ends, it is dropped with
  L26 `wifi`. It is not a summary: it changes no due slot (FR-25.3) and clears
  no readings (FR-25.12).

### 28.3 Verification contracts

| ID | Precondition · stimulus | Expected observation | Must NOT happen | Tier |
|----|------------------------|----------------------|-----------------|------|
| FR-28.1, FR-28.2 (core) | Bodies: none; `setup=std`; `setup=quick%2Cstd`; `setup=std&setup=quick&setup=cycle`; `setup=std,fine`; `setup=quick`; `setup=quick,quick,std`; `setup=nope`; `setup=custom&sample_min=5&average_min=30`; `sample_min=15`; `setup=std&sample_min=0` | std; std; std + quick; std + quick + cycle; `setup`; `setup`; `setup`; `setup`; custom 5 / 30; custom 15 / 60; std | A second normal id; a repeated test; `sample_min` refused under `std` | host |
| FR-28.3 (core) | `settings` read with std + cycle; custom 5 / 30 | `…&setup=std,cycle&sample_min=10&average_min=60`; `…&setup=custom&sample_min=5&average_min=30` | Tests already run listed | host |
| FR-28.4 (core) | Double-press detector on 20 ms samples: two 100 ms presses 300 ms apart; a press of 1.2 s then one of 100 ms; two presses 1.5 s apart; a key held from the first sample, then one press | Fires at the second release; none; none; none | Firing on a single press or a long hold | host |
| FR-28.5, FR-28.6, FR-28.9 (core) | Runner: quick + cycle + std started; 12 quick readings, then 20 cycle readings | Quick: 12 readings 10 s apart, awake; cycle: sleep 30 s after readings 1 – 19, none after 20; both cleared; then std | A 13th quick reading; a sleep after the last cycle reading | host |
| FR-28.8 (core) | Runner: a key during quick; a key wake during cycle; a boot that is not a wake with a test running | Fail `by=key`, remaining dropped; fail `by=key`; fail `by=reset` | The test resumed; the remaining tests run | host |
| FR-28.10, FR-28.11 (core) | Verdicts: 12 / 12; 11 / 12 (one L22); 20 / 20 with an average missing; 32 / 32 | `Test pass 12/12`; `Test FAIL 11/12`; `Test FAIL 20/20`; `Test pass 32/32` | More than 25 characters | host |
| FR-28.12 (core) | L32 and L33 for quick passed, cycle failed by key | `web: test start id=quick`; `web: test end id=quick readings=12/12 averages=0/0 result=pass`; `… id=cycle readings=3/20 averages=0/1 result=fail by=key` | Another field order | host |
| FR-28.6, FR-28.7, FR-28.11 | Bench: BLE write `setup=cycle,std`, then let it run | L32, then 20 L21 lines 30 – 33 s apart with `web: wake cause=timer` between, L33 `result=pass`, L2 and line 5 `Test pass 20/20`, and `/api/history` holds all 20 | A missing L21; a sleep after the 20th reading; WiFi or panel on a timer wake | bench |
| FR-28.4, FR-28.6 | Bench: ONLINE; operator double-presses the user key | L32 `id=quick` within 1 s, 12 L21 lines 10 s apart, L33 `result=pass`, no L27 | A BLE window; the task running during the test | bench (operator) |
| FR-28.8 | Bench: during `cycle`, operator presses the user key | L33 `result=fail by=key`, line 5 `Test FAIL <n>/20` | The test continuing | bench (operator) |
| FR-28.13 | Bench: mail set up, `quick` | L25 within the window; one email with subject `<hostname> test pass` | A changed summary slot | bench (needs internet on the bench AP, R-19) |

---

## 29. 4-20 mA Loop Input

### 29.1 Purpose & peer

An optional input that powers and reads one 2-wire 4-20 mA transmitter (an
industrial pressure, level, flow or temperature sensor) at each reading, and
records its value with the BME280 reading. Peer: the transmitter, wired to a
2-pin terminal on the isolated side of the loop add-on board. It is off by
default; a device without the loop hardware runs exactly as before. Built on
the shared `ads1115` component (pure register core, thin I2C layer) and the
shared `loop420` component (pure scaling and fault core); the logic layer
decides when to read, the composition root switches the supply and reads.

The loop side is galvanically isolated from the board. At each reading the
board switches on an isolated 24 V supply, waits `loop_warm_ms`, reads the
voltage over a 100 Ω shunt with its own ADS1115 behind an I2C isolator, and
switches the supply off.

| Item | Value | Source |
|------|-------|--------|
| Loop supply | 24 V, isolated 1 W DC-DC fed from the battery/system rail through a wide-input converter (not from 5 V, which is not live on battery), behind a load switch with a controlled rise time, so the converters' inrush does not brown out the board on battery | [user] |
| Isolated 3.3 V | An LDO fed from the isolated 24 V, rated for at least 40 V in (the unregulated 24 V rises at light load), powering the loop ADS1115 and the isolator's side 2 | [user] |
| Loop protection | A current limit of about 30 mA in the loop, so a shorted transmitter or cable keeps the shunt voltage, and so the ADS1115 input, within the ADS1115's rating; a TVS diode across the terminals | [user] |
| Supply enable | GPIO 3 (header pin 4), active high, pulled low through sleep and reset | [user] |
| Shunt | 100 Ω, 0.1 %, 25 ppm/°C; 0.4 V at 4 mA, 2.0 V at 20 mA | [user] |
| Loop ADC | ADS1115 at 0x49 on the isolated side, differential A0 – A1, ±4.096 V range, single shot at 128 SPS (DR 100, about 8 ms a conversion) | [user] |
| Bus | Its own I2C bus, controller 1 on SDA GPIO 15 and SCL GPIO 16 at 100 kHz, through an I2C isolator whose board side (VCC1 and its pull-ups) is powered from GPIO 3, so it draws nothing while the loop is off; the bus is opened only for a loop read and its pins float otherwise | [user] |
| Board | The whole loop circuit, the isolated side, the loop ADS1115 and the isolator included, on its own add-on board, so the isolation barrier lies within that board; it reaches the carrier through the 5-pin loop connector of §2.2, and GPIO 3's 100 kΩ pull-down sits on the add-on | [user]; connector [proposed] |

Settings, in the portal form (§13.2) and a BLE `settings` write (§26):

| Field | Format | Valid | Empty or absent |
|-------|--------|-------|-----------------|
| `loop` | `off` or `in` | either | `off` |
| `loop_name` | label for the screen, page, CSV column and email | 1 – 12 characters from `A-Z a-z 0-9 _ -` | `loop` |
| `loop_unit` | unit text | 0 – 6 printable ASCII characters, no comma | `mA` |
| `loop_lo`, `loop_hi` | the value at 4 mA and at 20 mA | decimal, at most 3 decimals, −99999 – 99999, not equal | 4 and 20 (the value is the current) |
| `loop_warm_ms` | settle time after the supply turns on | whole ms, 50 – 5000 | 300 [user] |

### 29.2 Add-on values

A reading carries up to two optional **add-on values** beside the BME280
triple: slot 1 for the RS232 input (§30) and slot 2
for the loop. Each is a 32-bit float, NaN when the add-on is off or gave no
value, so every reading has the same size in RTC memory and in the history.

Usually only one add-on is connected at a time, so each is turned on or off
on its own, and only those that are on are read, counted in a §28 verdict,
given a CSV column, an `ext` key, an average, a summary line and a place on
line 6. An add-on that is off leaves no trace anywhere (FR-29.14).

| Where | An enabled add-on shows as |
|-------|----------------------------|
| `GET /api/readings` (FR-23.18) | `"ext": {"<rs_name>": <value or null>, "<loop_name>": <value or null>}` in each element, only the enabled ones; `{}` when every add-on is off |
| History CSV (§27.1) | One column after `p_mbar` per enabled add-on, slot 1 first, named `<rs_name>` (`rs_dec` decimals) or `<loop_name>` (3 decimals), empty for none; the header and every line gain it only while the add-on is on |
| Averages (FR-23.15) | A mean of the period's values with its own count, readings without a value left out |
| Summary email (§25) | The add-on's min, max and mean beside the BME280's, with its name and unit |
| L21 | Unchanged; each add-on has its own line, L37 (RS232) and L34 (loop) |

### 29.3 Requirements

Reading:

- **FR-29.1** [Must] [user] With `loop` `off` the device shall leave the
  loop supply off, never address 0x49, and behave as without §29.
- **FR-29.2** [Must] [user] With `loop` `in`, at each reading (FR-23.1,
  FR-23.3, §28 tests) the device shall, after the BME280 reading, turn the
  loop supply on, wait `loop_warm_ms`, take the mean of **4**
  single-shot conversions of A0 – A1 at 128 SPS, and turn the supply off.
- **FR-29.3** [Must] [user] The current shall be the shunt voltage over
  100 Ω; the value shall be `loop_lo + (mA − 4) / 16 × (loop_hi − loop_lo)`,
  unclamped, for a current from 3.6 to 21.0 mA.
- **FR-29.4** [Must] [user] A current below **3.6 mA** or above **21.0 mA**
  (NAMUR NE43) shall be a loop fault: no value is kept and L35 is logged with
  the current. An ADS1115 that does not answer, or a failed transfer, shall
  log L35 with its error. Neither stops the BME280 reading, the window or
  sleep, and neither is retried within the same task run.
- **FR-29.5** [Must] [derived] Each loop value shall be logged as L34; in
  ONLINE it reaches the UDP log by FR-16.1.
- **NFR-29.1** [Must] [user] The value shall be within **±0.25 % of span**
  of a reference current from 4 to 20 mA, at room temperature.
- **NFR-29.2** [Must] [user] The loop supply shall be on at most
  `loop_warm_ms` + **50 ms** per reading, and off through sleep,
  reset and power-on.
- **C-29.1** [Must] [user] The loop side shall be galvanically isolated from
  the board's ground, including the loop ADS1115 and its supply.
- **C-29.2** [Must] [user] With the loop supply off, the loop hardware
  shall draw no current from the board (the isolator unpowered, GPIO 15/16
  floating without pulls), and the BME280's bus (GPIO 43/44) shall not be
  touched by §29.
- **C-29.3** [Must] [user] A short across the loop terminals shall damage
  nothing: the loop current stays at or below **30 mA**, the reading ends
  with L35 `fault`, and the next reading after the short is removed is
  valid again.

Recording and showing:

- **FR-29.6** [Must] [user] The loop value shall be slot 2 of §29.2: kept with
  its reading (FR-23.9), averaged with its own count (FR-23.15), and appended
  to the history with it (FR-27.1).
- **FR-29.7** [Must] [user] In ONLINE, `GET /` shall show the newest loop
  value with `loop_name` and `loop_unit`, or say that none is kept.
- **FR-29.8** [Must] [user] With `loop` `in`, line 6 of the screen (FR-6.14)
  shall show `<loop_name> <value> <loop_unit>`, the value with at most 3
  decimals, or `<loop_name> --` when the newest reading has none; with `loop`
  `off` line 6 is empty.
- **FR-29.9** [Must] [user] When mail is set up (§25), the summary shall add
  one line `<loop_name> min <v> max <v> mean <v> <loop_unit> (<n> values)`
  after the BME280's min and max, and a loop column to the averages table
  (FR-25.15), empty for a period with no value.
- **FR-29.10** [Must] [user] With `loop` `off` the loop shall play no part
  in a §28 test. With `loop` `in`, a test reading shall count (FR-28.10) only
  if it also has a loop value, and the test's L33 line shall end with
  ` <loop_name>=<v>/<n>`, v the test readings with a loop value and n its
  readings, so the result email (FR-28.13) reports it too. A missing loop
  value never stops data collection: the BME280 reading is kept and
  appended as usual.
- **FR-29.14** [Must] [user] Each add-on of §29.2 shall be turned on and off
  on its own; one that is off shall add nothing to the readings' `ext`, the
  CSV, the averages, the summary, line 6, the log or a §28 verdict.

Settings and history format:

- **FR-29.11** [Must] [user] The portal and BLE shall refuse a `loop` field
  not valid per §29.1 with 400 naming the field (portal) or `invalid=<field>`
  (BLE); valid values are stored in NVS with the other settings (T3, T14),
  reported by the `settings` read (FR-26.9) and erased by a factory reset
  (T10). Other fields are checked only with `loop` `in`.
- **FR-29.12** [Must] [user] The history shall hold 24-byte records
  (§27.1). On the first boot of a firmware with this format, a history
  written in the old 16-byte format shall be erased once and L36 logged; a
  reset during that erase finishes it on the next boot (as FR-27.7).
- **FR-29.13** [Must] [derived from FR-27.8] A device whose `history`
  partition is smaller than C-10.10 (one updated only by OTA) shall use the
  partition it has, at its own size.

### 29.4 Verification contracts

| ID | Precondition · stimulus | Expected observation | Must NOT happen | Tier |
|----|------------------------|----------------------|-----------------|------|
| FR-29.3, FR-29.4 (core) | Shunt voltages for 3.5, 3.6, 4, 12, 20, 21.0, 21.1 mA with `loop_lo` 0, `loop_hi` 100; then `loop_lo` 100, `loop_hi` 0 | Fault; −2.5; 0; 50; 100; 106.25; fault; the reversed scale mirrored | A value for a fault current; clamping inside 3.6 – 21.0 mA | host |
| FR-29.2 (core) | `ads1115` core: register writes for one conversion of A0 – A1 at ±4.096 V | Config `MUX` 000, `PGA` 001, `MODE` single shot, `DR` 100 (128 SPS), then a read of the conversion register | Continuous mode; another channel, range or data rate | host |
| FR-29.11 (core) | Bodies: none; `loop=in`; `loop=on`; `loop_name=a%20b`; 13-character name; `loop_unit=a,b`; `loop_lo=5&loop_hi=5`; `loop_warm_ms=49`; 5001; `loop=off&loop_lo=x` | off; in with defaults; 400 `loop`; 400 `loop_name`; 400 `loop_name`; 400 `loop_unit`; 400 `loop_hi`; 400 `loop_warm_ms` twice; off (not checked) | A stored invalid value | host |
| FR-29.6, §29.2 (core) | History records and JSON with the loop off, on with a value, on with none | CSV without the column; with `<loop_name>` and 3 decimals; an empty field. JSON `ext` `{}`, a number, null | A column while off; `NaN` in JSON or CSV | host |
| FR-29.8 (core) | Line 6 for `Tank`, 12.3456, `psi`; none; a 12-character name with −99999.999 and a 6-character unit | `Tank 12.346 psi`; `Tank --`; at most 44 characters | Wrapping; more than 44 characters | host |
| FR-29.10, FR-29.14 (core) | Verdicts: `quick` 12 / 12 readings with the loop off; on with 12 values; on with 11 values | `result=pass` without a loop field; `result=pass … loop=12/12`; `result=fail … loop=11/12` | A loop field or loop effect while off | host |
| FR-29.9 (core) | Summary with 3 loop values and one reading without | The loop line with n = 3; the averages column empty where none | A NaN printed | host |
| FR-29.12 (core) | `flash_ring` holding 16-byte records, then opened with the 24-byte format | Erased once, L36; a second open keeps the new records | A mixed history; erasing on every boot | host |
| FR-29.2, FR-29.5, NFR-29.2 | Commissioning rig: a 4-20 mA calibrator in place of the transmitter, set to 12 mA; reset over USB | L34 with `ma=` within 12.000 ± 0.040; the enable GPIO high for at most 350 ms, measured on the bench's logic input or a scope | No L34; the supply on during sleep | bench (operator) |
| NFR-29.1 | Calibrator at 4, 8, 12, 16, 20 mA, `loop_lo` 0, `loop_hi` 100 | Each L34 value within ±0.25 of the set point's value | An error over 0.25 | bench (operator) |
| FR-29.4 | Calibrator at 3.0 mA, then the terminal open, then the ADS1115 unplugged | L35 `fault ma=3.000`; L35 `fault ma=0.000`; L35 with `ESP_ERR_TIMEOUT`; the window and sleep as usual | A restart; a value kept | bench (operator) |
| FR-29.10, FR-29.14 | Double press for `quick`, first with `loop` `off` and nothing on the terminals, then `in` with nothing on them | `result=pass` and no L34/L35; then `result=fail … loop=0/12` with all 12 readings in the history | A loop line while off; a reading missing from the history | bench (operator) |
| C-29.1 | Board on USB; ohmmeter between the loop terminals and USB ground | Above 100 MΩ | Continuity | bench (operator) |
| C-29.2 | Loop hardware fitted, `loop` `off`, then `in` with the calibrator at 12 mA; 5 readings each; the sleep current measured with and without the loop hardware | Every reading logs L21 with BME280 values; the loop commissioning program finds 0x76 on GPIO 43/44 and 0x49 only on GPIO 15/16 with the supply on; the sleep current unchanged within 10 µA | A BME280 or bus error; SDA or SCL held low | bench (operator) |
| C-29.3 | `loop` `in`; the terminals shorted for 3 readings, then the calibrator at 12 mA | L35 `fault` with `ma=` at most 30.000 for each shorted reading; then L34 within 12.000 ± 0.040 | A restart; a value kept while shorted; a part hot to the touch | bench (operator) |
| FR-29.7, FR-29.8, §29.2 | ONLINE with the calibrator at 12 mA; relay `GET /`, `GET /api/readings`, `GET /api/history` | Page, `ext`, CSV column and line 6 all show the L34 value | A value differing from L34 | bench |
| FR-29.12 | Flash over a board holding a 16-byte history (all four images), then ONLINE | L36 once; `GET /api/history` holds only new readings | A garbled line; L36 on the next boot | bench |

---

## 30. RS232 Instrument Input

### 30.1 Purpose & peer

An optional serial port that reads one RS232 instrument at each reading and
records its value with the BME280 reading, as add-on slot 1 of §29.2. Peer:
an instrument with a text protocol (a meter, balance, analyser or
controller) that answers a poll command, or sends lines by itself, at a
fixed baud rate. Which instrument is not known yet, so the settings are
generic. The port is off by default; a device without the RS232 hardware
runs exactly as before. Built on the shared `serial_port` component (UART
and transceiver power, with a pure line and number core); the composition
root powers the port, reads it and routes the value.

The transceiver is powered only while the port is read: at each reading the
board sets GPIO 13 high, which is the transceiver's supply, waits for its
charge pump, opens UART1, exchanges one poll or listens, closes the UART,
lets GPIO 11 and 12 float, and sets GPIO 13 low. Every part is through-hole
or panel-mount, so the circuit builds on perfboard.

| Item | Value | Source |
|------|-------|--------|
| UART | UART1, no hardware flow control | [user] |
| TX / RX | GPIO 11 (to the transceiver's T1IN) / GPIO 12 (from R1OUT); ADC2 pins, so no ESP32 ADC input is lost | [user] |
| Transceiver power | GPIO 13 is the transceiver's VCC, pulled low (100 kΩ) through sleep and reset; the transceiver draws 0.3 mA typical, about 3 – 6 mA driving an instrument | [user] |
| Charge-pump settle | **10 ms** [proposed] after GPIO 13 goes high, before UART1 sends or listens | [proposed] |
| Transceiver | Analog Devices MAX3232ECPE+ in a DIP socket (PDIP-16, 3.0 – 5.5 V, two drivers and two receivers, ±15 kV ESD); MAX3232EPE+, MAX3232EEPE+ or MAX3232CPE+ fit the same socket | [user] |
| RTS | Optional: the second driver's input tied to GND holds DE-9 pin 7 (RTS) asserted while the port is powered, for instruments that need it | [proposed] |
| Connector | DE-9 male, panel mount with solder cups, DTE: pin 2 RXD, pin 3 TXD, pin 5 GND; or a 3-pin 5.08 mm screw terminal (TX, RX, GND) | [user] |
| Board | The transceiver, its capacitors, GPIO 13's 100 kΩ pull-down and the instrument connector on their own add-on board, reaching the carrier through the 4-pin RS232 connector of §2.2 | [user]; connector [proposed] |

Settings, in the portal form (§13.2) and a BLE `settings` write (§26):

| Field | Format | Valid | Empty or absent |
|-------|--------|-------|-----------------|
| `rs` | `off`, `poll` (send `rs_cmd`, read the reply) or `listen` (read what the instrument sends by itself) | one of the three | `off` [user] |
| `rs_baud` | bits per second | 1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200 | 9600 [user] |
| `rs_format` | data bits, parity, stop bits | `8N1`, `8E1`, `8O1`, `7E1`, `7O1`, `8N2` | `8N1` [user] |
| `rs_cmd` | poll command, with `\r`, `\n` and `\xHH` escapes | 1 – 32 [proposed] bytes after the escapes; required with `poll` | none |
| `rs_timeout_ms` | wait for a whole reply line | whole ms, 100 – 5000 [proposed] | 1000 [proposed] |
| `rs_field` | which number in the line is the value | 1 – 8 [proposed] | 1 |
| `rs_scale`, `rs_offset` | value = number × `rs_scale` + `rs_offset` | decimal, at most 6 decimals, −99999 – 99999, `rs_scale` not 0 | 1 and 0 |
| `rs_name` | label for the screen, page, CSV column and email | 1 – 12 characters from `A-Z a-z 0-9 _ -`, not equal to `loop_name` while both add-ons are on | `rs232` |
| `rs_unit` | unit text | 0 – 6 printable ASCII characters, no comma | empty |
| `rs_dec` | decimals of the value on the screen, page, JSON, CSV and email | 0 – 4 [proposed] | 3 |

A line ends at CR, LF or CRLF and holds at most **128** [proposed] bytes. A
number is an optional sign, digits with an optional decimal point, and an
optional exponent (`1.5e-3`); the fields are counted from the start of the
line, so `ST,GS,+0012.34 g` gives 12.34 as field 1.

### 30.2 Requirements

Reading:

- **FR-30.1** [Must] [user] With `rs` `off` the device shall keep GPIO 13
  low, never install UART1, leave GPIO 11 and 12 floating without pulls,
  and behave as without §30.
- **FR-30.2** [Must] [user] With `rs` not `off`, at each reading (FR-23.1,
  FR-23.3, §28 tests) the device shall, after the BME280 reading, set GPIO 13
  high, wait the charge-pump settle of §30.1, open UART1 at `rs_baud` and
  `rs_format`, take one value by FR-30.3 or FR-30.4, then close UART1, leave
  GPIO 11 and 12 floating without pulls, and set GPIO 13 low.
- **FR-30.3** [Must] [user] With `rs` `poll` the device shall discard any
  input already received, send `rs_cmd`, and take the first whole line that
  arrives within `rs_timeout_ms` of the command.
- **FR-30.4** [Must] [user] With `rs` `listen` the device shall take the
  last whole line that arrives within `rs_timeout_ms` of the port opening.
- **FR-30.5** [Must] [user] The value shall be the `rs_field`-th number in
  the line (§30.1) times `rs_scale` plus `rs_offset`.
- **FR-30.6** [Must] [user] No whole line in time, a line over 128 bytes, or
  no number at `rs_field` shall give no value and log L38 with the reason and
  the first 32 bytes of the line, non-printable bytes as `\xHH`. None of them
  stops the BME280 reading, the window or sleep, and none is retried within
  the same task run.
- **FR-30.7** [Must] [derived] Each RS232 value shall be logged as L37; in
  ONLINE it reaches the UDP log by FR-16.1.
- **NFR-30.1** [Must] [user] With `rs` not `off`, GPIO 13 shall be high at
  most `rs_timeout_ms` + **50 ms** [proposed] per reading, the 10 ms settle
  included.
- **C-30.2** [Must] [user] With GPIO 13 low, the RS232 hardware shall draw
  no current from the board (the transceiver unpowered, GPIO 11 and 12
  floating without pulls, so TX cannot power the transceiver through its
  input); the sleep current shall be unchanged within 10 µA with the
  hardware fitted.

Recording and showing:

- **FR-30.8** [Must] [user] The RS232 value shall be slot 1 of §29.2: kept
  with its reading (FR-23.9), averaged with its own count (FR-23.15), and
  appended to the history with it (FR-27.1); in `ext`, the CSV and the email
  it is named `rs_name` and shown with `rs_dec` decimals, its CSV column
  before the loop's.
- **FR-30.9** [Must] [user] In ONLINE, `GET /` shall show the RS232 mode,
  the newest value with `rs_name` and `rs_unit`, or the reason of the last
  L38 when the newest reading has none.
- **FR-30.10** [Must] [user] With `rs` not `off`, line 6 of the screen
  (FR-6.14) shall show `<rs_name> <value> <rs_unit>`, or `<rs_name> --` when
  the newest reading has none. With the loop on too, line 6 shall show the
  RS232 part, two spaces and the loop part of FR-29.8, cut at 44 characters.
- **FR-30.11** [Must] [user] When mail is set up (§25), the summary shall
  add one line `<rs_name> min <v> max <v> mean <v> <rs_unit> (<n> values)`
  before the loop's (FR-29.9), and an RS232 column to the averages table
  (FR-25.15), empty for a period with no value.
- **FR-30.12** [Must] [user] RS232 shall follow FR-29.14: with `rs` `off`
  it plays no part in a §28 test. With `rs` not `off`, a test reading shall
  count (FR-28.10) only if it also has an RS232 value, and the test's L33
  line shall end with ` <rs_name>=<v>/<n>` (v the test readings with an
  RS232 value, n its readings), before the loop's suffix of FR-29.10, so the
  result email (FR-28.13) reports it too. A missing value never stops data
  collection: the BME280 reading is kept and appended as usual.

Settings:

- **FR-30.13** [Must] [user] The portal and BLE shall refuse an `rs` field
  not valid per §30.1 with 400 naming the field (portal) or `invalid=<field>`
  (BLE); valid values are stored in NVS with the other settings (T3, T14),
  reported by the `settings` read (FR-26.9) and erased by a factory reset
  (T10). Other fields are checked only with `rs` not `off`.
- **C-30.1** [Must] [user] UART1 on TX GPIO 11, RX GPIO 12, and the
  transceiver's supply on GPIO 13, as in §30.1; the pins are confirmed by
  a loopback commissioning program before the first build that uses them.

### 30.3 Verification contracts

| ID | Precondition · stimulus | Expected observation | Must NOT happen | Tier |
|----|------------------------|----------------------|-----------------|------|
| FR-30.5, FR-30.6 (core) | Lines `12.5\r\n`, `ST,GS,+0012.34 g\r`, `-1.5e-3\n`, `A=1 B=2 C=3` with field 3, `no number\r`, 129 bytes without an end, `\x02 7.00 pH\r`; scale 2, offset −1 | 24; 23.68; −1.003; 5; L38 `nonum`; L38 `long`; 13 | A value from a cut or numberless line; a number taken from inside a word | host |
| FR-30.3, FR-30.4 (core) | `serial_port` core fed bytes in pieces: a line split over three reads; CR, LF and CRLF ends; two lines within the timeout in `listen` | One whole line each time; `listen` takes the second | A half line; an empty line as a value | host |
| FR-30.13 (core) | Bodies: none; `rs=poll` without `rs_cmd`; `rs=poll&rs_cmd=%5Cr`; `rs_baud=9601`; `rs_format=9N1`; `rs_timeout_ms=99`; 5001; `rs_field=9`; `rs_scale=0`; `rs_name=loop` with the loop on and `loop_name=loop`; `rs_unit=a,b`; `rs_dec=5`; `rs=off&rs_baud=x` | off; 400 `rs_cmd`; poll with a 1-byte command; 400 for each named field; off (not checked) | A stored invalid value | host |
| FR-30.8, §29.2 (core) | JSON and CSV with RS232 off; on with a value and `rs_dec` 0 and 4; on with none; RS232 and loop both on | No key or column; `rs_name` with 0 and 4 decimals; null and an empty field; RS232 column before the loop's | A key or column while off; `NaN` in JSON or CSV | host |
| FR-30.10 (core) | Line 6 for `pH` 7.0234 `pH` with `rs_dec` 2; none; RS232 and loop both on with 12-character names and units | `pH 7.02 pH`; `pH --`; both parts, at most 44 characters | Wrapping; more than 44 characters | host |
| FR-30.12, FR-29.14 (core) | Verdicts: `quick` with RS232 off; on with 12 values; on with 11 values; RS232 and loop both on | `result=pass` without an RS232 field; `… rs232=12/12`; `result=fail … rs232=11/12`; the RS232 suffix before the loop's | An RS232 field or effect while off | host |
| FR-30.11 (core) | Summary with 3 RS232 values and one reading without | The RS232 line with n = 3, before the loop line; the averages column empty where none | A NaN printed | host |
| C-30.1, FR-30.2 | Commissioning program: GPIO 11 jumpered to GPIO 12 at 3.3 V (no transceiver); send and read 64 bytes at every baud and format of §30.1 | Every byte back; GPIO 13 toggles as commanded | A lost byte; a pin that does not move | target |
| FR-30.1, C-30.2 | RS232 hardware fitted, shipped build with `rs` `off`; then `poll` with nothing on the DE-9; sleep each time | No UART1 install in the log; GPIO 13 low; transceiver VCC below 0.3 V while off and asleep (a meter on pin 16); the sleep current with and without the hardware equal within 10 µA | GPIO 13 high while off; VCC raised through GPIO 11 | bench (operator) |
| FR-30.2, FR-30.3, FR-30.7, NFR-30.1 | Transceiver wired (`rs232-circuit.md`); the testbench host's USB-RS232 adapter on the DE-9 runs a scripted instrument answering `R\r` with `+0023.45 C\r`; `rs=poll&rs_cmd=R%5Cr`; reset over USB | L37 `rs232 rs232=23.450`; the script saw one `R\r` per reading; GPIO 13 high at most 1050 ms | No L37; a second command in one reading | bench |
| FR-30.4 | As above, the script sending `+0023.45 C\r` every 200 ms; `rs=listen` | L37 with the last line's value | A half line | bench |
| FR-30.6 | As above, the script silent, then answering `ERR\r`, then 200 bytes without CR, then unplugged | L38 `timeout`, `nonum line=ERR`, `long`, `timeout`; the window and sleep as usual; each BME280 reading still in the history | A restart; a value kept | bench |
| FR-30.12 | Double press for `quick`, first with `rs` `off` and nothing on the DE-9, then `poll` with the script silent | `result=pass` and no L37/L38; then `result=fail … rs232=0/12` with all 12 readings in the history | An RS232 line while off; a reading missing from the history | bench |
| FR-30.8, FR-30.9, FR-30.10, §29.2 | ONLINE with the script answering; relay `GET /`, `GET /api/readings`, `GET /api/history` | Page, `ext`, CSV column and line 6 all show the L37 value | A value differing from L37 | bench |

---

## 31. Push Alerts

### 31.1 Purpose & peer

The device sends a short push notification to the user's phone when
something needs attention. A reading crosses a limit the user set, or comes
back inside it. The sensor, an add-on or the history fails, or recovers.
A §28 test ends. It does not send routine readings: the summary stays email
(§25). Peer: an ntfy server, self-hosted on the LAN or reachable over the
internet. The device publishes each alert with one HTTP POST to
`<ntfy_url>/<ntfy_topic>`, and the user's ntfy app, subscribed to that
topic, shows it. Built on a shared `ntfy_tx` component (pure core for the
request and the reply, a thin `esp_http_client` transport). The pure
`notify_core` decides which alerts are due from the readings and the stored
alert state; the composition root sends them.

Alerts are a selectable item: they run only while `ntfy` is `on`. With
`ntfy` `off` the device behaves as without §31 and keeps the stored server,
topic, token and limits, so alerts can be turned back on without entering
them again.

### 31.2 Settings

These fields go in the portal form (§13.2) and a BLE `settings` write
(§26). All are optional as a group, so a submission without them stays
valid and leaves alerts off.

| Field | Format | Valid | Empty or absent |
|-------|--------|-------|-----------------|
| `ntfy` | alerts `on` or `off` | one of the two; `on` needs `ntfy_url` and `ntfy_topic`, given or stored | the stored value; `off` when none is stored [user] |
| `ntfy_url` | server base URL, `http://` or `https://`, host, optional `:port`, no path, query or trailing `/`; e.g. `http://192.168.2.231` | 10 – 96 bytes; host of letters, digits, `.` and `-`; port 1 – 65535 | none |
| `ntfy_topic` | topic name | 1 – 64 characters from `A-Z a-z 0-9 _ -` (ntfy's own limit); required with `ntfy_url` | none |
| `ntfy_token` | access token, sent as `Authorization: Bearer <token>` | 1 – 64 printable ASCII characters, no space | no `Authorization` header |
| `alert_t_lo`, `alert_t_hi` | temperature limits, °F | decimal, at most 1 decimal, −40.0 – 185.0 | no alert on that side |
| `alert_rh_lo`, `alert_rh_hi` | humidity limits, % | whole number, 0 – 100 | no alert on that side |
| `alert_p_lo`, `alert_p_hi` | pressure limits, mbar | decimal, at most 2 decimals, 300.00 – 1100.00 | no alert on that side |

With both limits of a value given, `hi − lo` shall be at least twice that
value's hysteresis (FR-31.4), so the two alerts cannot overlap.

### 31.3 Alerts

| Event | Starts when | Ends when (sends the `ok` alert) | Title |
|-------|-------------|----------------------------------|-------|
| `t_high`, `t_low` | A reading's temperature is above `alert_t_hi` / below `alert_t_lo` | It is at or below `alert_t_hi` − 1.0 °F / at or above `alert_t_lo` + 1.0 °F | `<hostname> temperature high` / `low`; `<hostname> temperature normal` |
| `rh_high`, `rh_low` | Humidity above `alert_rh_hi` / below `alert_rh_lo` | At or below `alert_rh_hi` − 2 / at or above `alert_rh_lo` + 2 | `<hostname> humidity high` / `low` / `normal` |
| `p_high`, `p_low` | Pressure above `alert_p_hi` / below `alert_p_lo` | At or below `alert_p_hi` − 1.00 / at or above `alert_p_lo` + 1.00 | `<hostname> pressure high` / `low` / `normal` |
| `sensor` | A BME280 reading fails (L22) | A BME280 reading succeeds | `<hostname> sensor failed` / `sensor ok` |
| `rs232` | An RS232 reading gives no value (L38), with `rs` not `off` | An RS232 reading gives a value | `<hostname> <rs_name> failed` / `ok` |
| `loop` | A loop reading fails (L35), with `loop` not `off` | A loop reading gives a value | `<hostname> <loop_name> failed` / `ok` |
| `history` | The history is unavailable or an append fails (L31) | An append succeeds | `<hostname> history failed` / `history ok` |
| `test` | A §28 test ends (L33) | (one message, no `ok`) | `<hostname> test pass` / `test FAIL` |

The body of a limit alert is
`<Temp|Hum|ATM> <value> <unit>, limit <limit> <unit>, <YYYY-MM-DD hh:mm>`,
the value in the decimals of FR-23.12 and the reading's local time, or
`no time` for a clock never set (FR-22.6). A failure alert's body is the
log line it came from after `web: `. A test alert's body is the L33 line
of each test that ran, after `web: `, one per line.

| Kind | Priority | Tags |
|------|----------|------|
| A condition starts (`high`, `low`, `failed`), `test FAIL` | 4 (high) | `warning` |
| A condition ends (`normal`, `ok`), `test pass` | 3 (default) | `white_check_mark` |

In L39 and L40 an alert is named by its event, with `_ok` added for an end
alert (`t_high_ok`, `sensor_ok`) and `test_pass` or `test_fail` for a
test.

Every message also carries `Click: http://<hostname>.local/`, so tapping it
opens the device page when the phone is on the device's LAN.

### 31.4 Requirements

Settings:

- **FR-31.1** [Must] [user] The portal form (§13.2) and a BLE `settings`
  write (§26) shall carry the fields of §31.2. Every §31.2 field given shall
  be valid, else the portal answers 400 naming the first invalid field and
  BLE answers `invalid=<field>`; `ntfy=on` without a URL and topic, given or
  stored, is invalid as `ntfy`.
- **FR-31.14** [Must] [user] Alerts shall be selectable: with `ntfy` `off`
  no alert is decided or sent, no WiFi is started for one, and L39 and L40
  never appear; the other §31.2 settings stay stored. Turning `ntfy` `on`
  starts every condition as normal (FR-31.5).
- **FR-31.2** [Must] [user] Valid settings shall be stored in NVS with the
  other settings (T3, T14), reported by the `settings` read (FR-26.9) without
  the token (`ntfy_token_set=0|1` instead), and erased by a factory reset
  (T10). In a key-window BLE write an absent `ntfy_token` keeps the stored
  one, as for the other secrets.
- **FR-31.3** [May] [user] When no topic is stored, the portal form shall
  show `tds3-` and 12 random lowercase letters as the suggested
  `ntfy_topic`, so the default topic is hard to guess.

Deciding:

- **FR-31.4** [Must] [user] After each reading the task takes (FR-23.1,
  FR-23.3, the readings of a §28 test), `notify_core` shall compare it with
  the limits of §31.2 and the conditions of §31.3, using hysteresis
  **1.0 °F**, **2 %** and **1.00 mbar**, and make an alert due when a
  condition starts or ends. A condition that holds on the next reading makes
  nothing due; a value with no limit set makes no alert.
- **FR-31.5** [Must] [user] The state of each condition (normal, active, or
  active but not yet delivered) shall be kept in RTC memory across sleep.
  After a power loss or reset every condition starts as normal.
- **FR-31.6** [Must] [user] An `ok` alert shall be due only when the
  condition's start alert was delivered; a condition that ends before its
  start alert was delivered makes nothing due.
- **FR-31.7** [Must] [user] When a §28 test ends (L33), the `test` alert
  shall be due once, beside the result email (FR-28.13) and independent of
  it.

Sending:

- **FR-31.8** [Must] [user] The device shall send each due alert as its own
  HTTP POST to `<ntfy_url>/<ntfy_topic>`, with the body of §31.3, the
  headers `Title`, `Priority`, `Tags` and `Click` of §31.3, and
  `Authorization: Bearer <ntfy_token>` when a token is stored; oldest due
  first. A `2xx` reply is delivered.
- **FR-31.9** [Must] [user] With an `https://` URL the device shall verify
  the server certificate against the ESP-IDF CA bundle and the host name, as
  for mail (FR-25.5). An `http://` URL is sent in plain text.
- **FR-31.10** [Must] [user] On a timer wake with an alert due, the task
  shall start WiFi as a station with the stored credentials, send the due
  alerts (and a due summary, FR-25.6, on the same connection to the AP),
  stop WiFi (FR-21.7) and sleep; the panel stays dark. On any other run the
  alerts go out over the WiFi already up once the device is ONLINE.
- **FR-31.11** [Must] [user] An alert that cannot be delivered (not ONLINE
  within **15 s** of being due, a DNS, TCP, TLS or certificate failure, a
  reply other than `2xx`, or no reply within **10 s** of the first alert's
  connection, a budget the alerts of one run share) shall be logged as L40
  with the reason and the event, and not retried in the same run. Its
  condition stays "not yet delivered", so the next run that still finds it
  tries once more; after a second failure it is dropped until the
  condition ends and starts again. A `test` alert is tried only once.
- **FR-31.12** [Must] [derived] Each delivered alert shall be logged as L39,
  each failed one as L40 (§7.2).
- **FR-31.13** [Must] [derived from §9.1] The device shall not output
  `ntfy_token` on serial, UDP, any HTTP response, any BLE read, any email or
  any alert (FR-9.3).
- **NFR-31.1** [Must] [user] A timer wake that sends alerts and no summary
  shall be back asleep within **50 s** of the wake, success or failure; one
  that sends both within **60 s**.
- **NFR-31.2** [Should] [user] An alert shall be delivered within
  **5 s** of the reading that made it due when the device is already ONLINE
  and the server answers.

### 31.5 Verification contracts

| ID | Precondition · stimulus | Expected observation | Must NOT happen | Tier |
|----|------------------------|----------------------|-----------------|------|
| FR-31.1 (core) | Bodies: none; `ntfy=on` alone with nothing stored; `ntfy=on` with URL and topic; each §31.2 field invalid in turn; `ftp://h`; a URL with a path; topic `a b`; `alert_t_lo=70&alert_t_hi=71`; `alert_rh_hi=101`; `ntfy=maybe` | off; 400 `ntfy`; on; 400 naming that field each time; 400 `alert_t_hi` (gap below 2.0); 400 `ntfy` | A stored invalid value; alerts on without a URL and topic | host |
| FR-31.14 (core) | Stored URL, topic and `alert_t_hi`; `ntfy=off`; a reading past the limit; then `ntfy=on` with no other field and the same reading | Off: nothing due, settings kept; on: `t_high` due | An alert or WiFi start while off; the URL or topic erased by `off` | host |
| FR-31.14 | Sleep test build, alerts set up, `ntfy=off` written over BLE, a timer wake with a reading past the limit | `web: wake cause=timer`, L21, `web: sleep`; no L39, L40 or WiFi start | WiFi on that wake | target |
| FR-31.4, FR-31.5, FR-31.6 (core) | `alert_t_hi` 90.0; readings 89.0, 90.5, 91.0, 89.5, 88.9, 90.1 °F; then the same with the 90.5 alert's send failed, then failed again; then 90.5 failed and 88.9 next | due `t_high` at 90.5; nothing at 91.0 and 89.5; `t_high_ok` at 88.9; `t_high` at 90.1. Failed: tried again at 91.0, then dropped. Failed then ended: nothing due | Two start alerts for one condition; an `ok` without a delivered start; a third try | host |
| FR-31.4 (core) | Every limit pair at its edge and one hysteresis step inside; a sensor fail and recover; RS232 and loop off with L38/L35-like inputs | Start and end exactly as §31.3; nothing for an add-on that is off | An alert for an add-on that is off (FR-29.14) | host |
| FR-31.8, FR-31.13 (core) | `ntfy_tx` core builds the request for `t_high` with and without a token; titles and bodies at their longest names | Method, path, the four headers and `Authorization` only with a token; body as §31.3; no request over 1024 bytes | The token in the body or title; a header missing | host |
| FR-31.11 (core) | `ntfy_tx` core maps replies 200, 401, 403, 429, 500, a cut reply | `ok`, `auth`, `auth`, `limit`, `server`, `timeout` | Success on a non-`2xx` reply | host |
| FR-31.8, FR-31.12, NFR-31.2 | Provisioned with `ntfy_url=http://192.168.2.231` (the user's test server) and a test topic, `alert_t_hi` set 1 °F below the room; ONLINE; the test polls `GET <url>/<topic>/json?poll=1` | L39 `t_high`; the message on the server with the title, priority 4 and the `warning` tag within 5 s of L21 | No message; two messages | bench (R-28) |
| FR-31.10, NFR-31.1 | Sleep test build, the same server and limit, a timer wake | `web: wake cause=timer`, L21, L39, `web: sleep` within 50 s; panel dark | WiFi on a timer wake with nothing due; L2 or L7 lines | target / bench (R-28) |
| FR-31.11 | Same build, `ntfy_url` pointing at an address with no server | L40 `connect` with the event, then `web: sleep` within 50 s; on the next timer wake one more try and L40; on the one after, nothing | A retry in the same run; a third try | target |
| FR-31.7 | Mail off, ntfy set; double press for `quick` | One `test pass` message with the L33 line as its body; L39 `test_pass` | A message per reading; the result email expected | bench (R-28) |
| FR-31.13, FR-31.2 | Provisioned with token `tk_<random 12 hex>`; read BLE `settings`; capture serial and UDP through a window and an alert | `ntfy_token_set=1`; the marker nowhere | The marker in any read, log or message | bench (operator) |
| FR-31.8 | The user's phone subscribed to the topic in the ntfy app | The alert shows with its title; tapping it on the LAN opens the device page | A missing title or body | bench (operator) |

---

## 32. ThingsBoard Telemetry

### 32.1 Purpose & peer

The device uploads its readings to the user's own self-hosted ThingsBoard
server, so they appear on ThingsBoard dashboards with the time each was taken.
Uploads go in batches every `tb_every` minutes over ThingsBoard's HTTP device
API, taken from the reading history (§27) after a stored upload mark, so
readings taken while WiFi or the server is down go up with a later batch and a
timer wake starts WiFi only once per batch. ThingsBoard keeps one value per key
and time, so a reading sent twice is stored once, which makes resending after a
failure or a reset harmless.

Peer: the ThingsBoard server named by `tb_url`, reached through the stored WiFi
network; the device authenticates with the access token of its ThingsBoard
device. The server is reached first by plain `http://` on the LAN and later by
`https://` with a Let's Encrypt certificate [user]. Alarms and notifications on
the uploaded readings are set up on the server with ThingsBoard's rule engine;
the firmware has no part in them (its own alerts are §31). Built on a shared
`tb_tx` component (pure core for the request bodies and the answer, thin HTTP
transport); the pure `telemetry_core` decides when an upload is due and what
each request holds, the composition root reads the history, sends and logs.

### 32.2 Settings

Added to the §13.2 form and the BLE `settings` write (§26); all optional, so a
submission without them leaves ThingsBoard as it was (off on a new device).

| Field | Format | Valid | Empty or absent |
|-------|--------|-------|-----------------|
| `services` | master switch for ntfy alerts (§31) and ThingsBoard uploads together, `on` or `off` | one of the two | the stored value; `on` when none is stored [user] |
| `tb` | uploads `on` or `off` | one of the two; `on` needs `tb_url` and `tb_token`, given or stored | the stored value; `off` when none is stored [user] |
| `tb_url` | server base URL, e.g. `http://192.168.1.20:8080` or `https://tb.example.org` | `http://` or `https://`, then a host name or IPv4 address, an optional `:port`, no path, 1 – 128 bytes | the stored value |
| `tb_token` | the access token of the device in ThingsBoard | 1 – 64 [proposed] letters, digits, `-` and `_` | the stored value (in a key-window BLE write an absent token keeps the stored one, as for the other secrets) |
| `tb_every` | minutes between uploads | whole minutes, 1 – 1440, dividing 1440 [proposed] | 60 [user] |
| `tb_remote` | settings from ThingsBoard's shared attributes (FR-32.16) `on` or `off` [user] | one of the two | the stored value; `off` when none is stored [user] |

### 32.3 Requests

| What | Method and path | Body (`application/json`) |
|------|-----------------|---------------------------|
| Telemetry | POST `<tb_url>/api/v1/<tb_token>/telemetry` | `[{"ts":<ms since the epoch>,"values":{"temp_f":<°F, 2 decimals>,"rh_pct":<%, 1 decimal>,"p_mbar":<mbar, 2 decimals>,"synced":<true\|false>}}, …]`, oldest first |
| Attributes | POST `<tb_url>/api/v1/<tb_token>/attributes` | `{"hostname":"<FR-20.1>","fw_version":"<V>","ip":"<a.b.c.d>","rssi":<dBm>,"setup":"<normal id of §28.1>"}` |
| Remote settings (FR-32.16) | GET `<tb_url>/api/v1/<tb_token>/attributes?sharedKeys=<the keys of FR-32.17, comma-separated>` | none; the answer is `{"shared":{"<key>":<value>, …}}` |
| Remote settings result (FR-32.19) | POST `<tb_url>/api/v1/<tb_token>/attributes` | `{"remote_applied":"<keys, comma-separated, or empty>","remote_error":"<the first invalid key, or empty>","remote_ts":<ms since the epoch>}` |

The values are those of the reading's history line (§27.1), so ThingsBoard
shows what L21 and `/api/history` show.

### 32.4 Requirements

Settings:

- **FR-32.1** [Must] [user] The portal form (§13.2) and a BLE `settings` write
  (§26) shall carry the fields of §32.2. A submission shall be valid only if
  every field given is valid per §32.2 and, with `tb` `on`, `tb_url` and
  `tb_token` are given or stored, else answer 400 naming the first invalid
  field (portal) or `invalid=<field>` (BLE).
- **FR-32.14** [Must] [user] Uploads shall be switchable: with `tb` `off` no
  upload or attributes request is due or sent, no WiFi is started for one, L41
  and L42 never appear, and the upload mark stays where it is; the other §32.2
  settings stay stored. Turning `tb` `on` resumes from the mark, so readings
  taken while it was off go with the next upload when the history still holds
  them.
- **FR-32.15** [Must] [user] The `services` master switch shall turn ntfy
  alerts (§31) and ThingsBoard uploads off together: with `services` `off`
  each behaves as with its own switch off (FR-31.14, FR-32.14) whatever that
  switch says, and the own switches keep their stored values; with `services`
  `on` each follows its own switch. The `settings` read (FR-26.9) reports
  `services=<on|off>`, and `GET /` shows each service as on, off, or off by
  the master switch.
- **FR-32.2** [Must] [user] Valid settings shall be stored in NVS with the
  other settings at T3, or by an accepted BLE write in a key window (T14),
  reported by the `settings` read (FR-26.9) with `tb=<on|off>`, and erased
  with the upload mark by a factory reset (T10).
- **FR-32.3** [Must] [derived from §9.1, FR-9.3] The device shall not output
  `tb_token` on serial, UDP, any HTTP response, any email or any BLE read; the
  `settings` read (FR-26.9) reports only `tb_token_set=<0|1>`, and log lines name
  the server without the token.

When:

- **FR-32.4** [Must] [user] An upload is due at local midnight (FR-22.2) and
  every `tb_every` minutes after it. The task (§21) shall upload on its first
  run at or after a due time; one upload carries every reading not yet
  accepted, so due times missed in between are never caught up one by one.
  With the clock never set (FR-22.6), no upload is due.
- **FR-32.5** [Must] [user] When an upload is due on a timer wake, the device
  shall start WiFi as a station with the stored credentials, upload, stop WiFi
  (FR-21.7) and sleep; the panel stays dark. When a summary (FR-25.6) is due
  on the same wake, one WiFi start serves both, the summary first.
- **FR-32.6** [Must] [user] On each boot with a window, once the device is
  ONLINE, it shall upload once whether or not an upload is due, and send the
  attributes of §32.3.

What:

- **FR-32.7** [Must] [user] An upload shall send, oldest first, every history
  reading (§27) after the upload mark, as telemetry records of §32.3 in
  requests of at most **100** [proposed] readings each. Readings with a clock
  never set (FR-22.6) are not sent; the mark moves past them.
- **FR-32.8** [Must] [user] The upload mark shall be the place in the history
  of the newest reading the server has accepted, so a clock set back skips no
  reading. It shall be stored in NVS and move only when the server answers 200
  to the request that carried that reading. When the history has dropped
  readings after the mark (FR-27.2), the upload starts at the oldest reading
  kept.
- **FR-32.9** [Must] [user] The device shall send each request to `tb_url` as
  given: on `https://` it shall verify the server certificate against the
  ESP-IDF CA bundle and the host name; on `http://` it sends in plain text.
- **FR-32.10** [Must] [derived from FR-27.8] A device with no `history`
  partition shall upload the readings kept in RTC memory (FR-23.9) not yet
  accepted, with the upload mark kept in RTC memory beside them.

Failures and limits:

- **FR-32.11** [Must] [user] A request that fails (no WiFi within **15 s**, a
  DNS, TCP, TLS or certificate failure, an answer 401 or 403, another 4xx, a
  5xx, or no answer within **10 s** [proposed]) shall end the upload and be
  logged as L42; it shall not be retried on that wake, the readings stay after
  the mark for the next upload, and the device goes on as usual.
- **FR-32.12** [Must] [user] On a timer wake the device shall start no new
  request later than **40 s** [proposed] after the wake; readings left over
  go with the next upload.
- **NFR-32.1** [Must] [derived from NFR-25.1 and FR-32.12] A timer wake that
  uploads, with or without a summary, shall be back asleep within **50 s** of
  the wake, success or failure.
- **FR-32.13** [Must] [derived] Each accepted request shall be logged as L41,
  each failed one as L42 (§7.2).

Remote settings (ThingsBoard shared attributes):

- **FR-32.16** [Must] [user] With `tb_remote` `on`, after each upload whose
  requests all succeed, the device shall ask the server for the shared attributes
  of FR-32.17 (§32.3) in the same network session, so a remote change needs no
  extra wake or WiFi start. With `tb_remote` `off`, or `tb` or `services` off,
  no such request is sent.
- **FR-32.17** [Must] [user] Only these settings shall be taken from shared
  attributes: the sampling setup and its fields (§28: `setup`, `sample_min`,
  `average_min`), `tb_every`, the alert limits of §31 (`alert_*`), `loop` and its `loop_*`
  fields (§29, §40.4) and the `rs_*` fields of §30. Network credentials, server
  addresses, tokens, `tb`, `tb_remote` and `services` are never taken, so a
  remote change cannot cut the station off from the server.
- **FR-32.18** [Must] [user] A shared attribute shall be applied only when
  its value differs from the value the device last received for that key, so a
  setting changed locally (portal or BLE) stays until the server's value
  changes. The values received are checked together by the rules of a BLE
  `settings` write (FR-26.7, §28 – §31); if any is invalid none is applied.
  Accepted values are stored as at T3 and take effect from the next reading.
- **FR-32.19** [Must] [user] After each remote settings request the device
  shall post the result attributes of §32.3, so the dashboard shows what was
  applied or refused, and log L64 or L65.
- **FR-32.20** [Must] [user] On a build whose uplink is the Notecard
  (`uplink` `cell`, §33), the request of FR-32.16 shall go through a Notehub
  proxy route to the same ThingsBoard URL (`web.get`), at the sync that sends
  the readings; every other rule of FR-32.16 – FR-32.19 applies unchanged.
- **FR-32.21** [Must] [user] A failed remote settings request (FR-32.11's
  failures) shall change no setting, be logged as L65, and not be retried on
  that wake; the upload it followed stands.

### 32.5 Verification contracts

| ID | Precondition · stimulus | Expected observation | Must NOT happen | Tier |
|----|------------------------|----------------------|-----------------|------|
| FR-32.1 (core) | Form bodies, nothing stored: none of the fields; `tb=on` with all valid; `tb=on` with `tb_url` only; `tb=on` with `tb_token` only; `tb_url` and `tb_token` without `tb`; `tb=maybe`; `tb_url=ftp://x`; `tb_url` with a path; a token with a space; `tb_every` 0, 7, 1441, empty | Off; on; `tb_token`; `tb_url`; stored, off; `tb`; `tb_url`; `tb_url`; `tb_token`; `tb_every` three times, then 60 | `on` accepted without a URL and token | host |
| FR-32.15 (core) | `services` × `ntfy` × `tb`, all eight combinations, at a due upload and a limit crossing | An alert only with `services` and `ntfy` both `on`; an upload only with `services` and `tb` both `on`; the own switches unchanged after `services` goes `off` and back `on` | A send with `services` `off` | host |
| FR-32.14 (core) | `telemetry_core` with `tb` `off` at a due time and in a window; then `on` with 30 readings after the mark | Nothing due, the mark unchanged; then one upload of the 30 | WiFi started, L41 or L42 with `tb` `off` | host |
| FR-32.3 (core) | `settings` read and L41/L42 with a token stored; a request URL logged | `tb_token_set=1`; the token in no line | The token in any output | host |
| FR-32.4 (core) | Due check, `tb_every` 60: 09:59 and 10:00 with the last upload at 09:00; 13:05 after three missed slots; the clock never set | Not due, due; one due; never due | Two uploads for one slot; an upload with the clock unset | host |
| FR-32.7, FR-32.8 (core) | `telemetry_core` over a history of 250 readings, mark at 0; the second request refused; then all accepted; a clock-never-set reading in the middle; a history that wrapped past the mark | Requests of 100, 100, 50, oldest first; the mark at 100 after the refusal; at 250 afterwards; that reading skipped and passed; start at the oldest kept | A reading sent out of order; the mark moved on a refusal | host |
| FR-32.7 (core) | `tb_tx` body for 0, 1 and 100 readings at the sensor's limits, synced and unsynced | Valid JSON of §32.3, numbers as the history CSV, ts in ms | Units in the values; a body over the request limit | host |
| FR-32.11 (core) | `tb_tx` answer mapping: 200, 401, 403, 400, 413, 500, no answer | ok, auth, auth, refused, refused, server, timeout | Success on anything but 200 | host |
| FR-32.5, FR-32.13, NFR-32.1 | Sleep test build, ThingsBoard on the bench (R-30), an upload due on the next timer wake | `web: wake cause=timer`, L41, `web: sleep` within 50 s; the readings readable through the ThingsBoard REST API with their times; panel dark | WiFi on a timer wake with nothing due; L2 or L7 lines | target |
| FR-32.8, FR-32.11 | Same build, ThingsBoard stopped for two uploads, then started | L42 twice, then one L41 carrying every reading taken meanwhile | A reading missing in ThingsBoard; a retry on the failing wake | target |
| FR-32.6 | Reset; device reaches ONLINE | L41 within the window; the attributes of §32.3 on the ThingsBoard device | No upload in the window | target |
| FR-32.9 | Operator's production server on `https://` with a Let's Encrypt certificate | L41; the readings on the operator's dashboard | A send to a server whose certificate fails | bench (operator) |
| FR-32.17, FR-32.18 (core) | Remote attribute sets: `setup=fine`; `sample_min=7` with `average_min=15`; `tb_token=x`; the same `setup=fine` again after a local change to `std`; a set with one invalid key | Applied; refused, nothing applied; `tb_token` ignored; not applied (unchanged on the server); refused, nothing applied, the invalid key named | A credential taken; a partial set applied | host |
| FR-32.16, FR-32.19 | ThingsBoard on the bench, `tb_remote` `on`; the shared attribute `setup` set to `fine` on the server; an upload due | L41, then L64 `applied=setup`; `remote_applied=setup` on the device's client attributes; the next readings at 1 minute | A remote request with `tb_remote` `off`; an extra WiFi start | target |
| FR-32.21 | Same, the server stopped after the telemetry request | L65; the settings unchanged | A setting changed on a failed request | target |

---

## 33. Cellular Uplink

### 33.1 Purpose & peer

Where no WiFi reaches the device, it sends its scheduled traffic over
cellular instead: the ThingsBoard uploads (§32), the push alerts (§31) and
the summary email (§25). The modem is a Blues Notecard Cell+WiFi
**NOTE-MBGLW** (LTE Cat-1 bis, global bands, 500 MB of data and 10 years of
service included) [user]. The device never opens a TCP connection over
cellular. Each send becomes a JSON *note* that the device hands to the
Notecard over I2C. The Notecard keeps it in its own flash and delivers it to
Blues Notehub at its next sync, and a Notehub *route* the user sets up passes
it on to the real peer: an HTTP POST to ThingsBoard, to ntfy, or to the mail
service of §33.4. So a timer wake only reads, adds the notes and sleeps
again; the radio work happens on the Notecard, which stays powered and idles
at about 18 µA while the ESP32-S3 sleeps.

The window (§21) is unchanged: panel, WiFi when credentials are stored, web
page, OTA and BLE work as before. Cellular replaces WiFi only for the senders
of §25, §31 and §32, and only while `uplink` is `cell`.

Peers: the Notecard on the I2C bus; Notehub, reached by the Notecard; the
routes' targets, reached by Notehub from the internet, so each target must
be reachable from the internet (R-33). Built on a shared `notecard`
component in esp-idf-components (Blues' `note-c` library over the ESP-IDF I2C
master driver, plus a pure core for the request bodies and the answers); the
pure `uplink_core` decides which transport each send uses and what each note
holds, the composition root performs it.

### 33.2 Hardware

| Item | Value | Source |
|------|-------|--------|
| Modem | Blues Notecard Cell+WiFi NOTE-MBGLW, hardware v2.5 (M.2 key E), the card the user owns; run on cellular only (FR-33.14) | [user] |
| Carrier | Blues Notecarrier-X v1.3, the carrier the user owns: M.2 socket, USB-C, JST-PH LiPo connector and charger, a Qwiic and an ESLOV I2C connector (ESLOV also carries ATTN); no onboard antennas | [user] |
| Bus | I2C at 0x17 on the BME280's bus (GPIO 43/44, 100 kHz, C-23.1) from the carrier's I2C fan-out (§2.2), wired as the Wiring row says, which also feeds the BME280 and the water probe board; no address clash with 0x76, the water ADS1115's 0x48 or the Notecarrier-X's LiPo charger (TI BQ25628E) at its fixed 0x6A, which shares the Notecard's I2C lines and which the firmware never addresses. The Notecard's own 10 kΩ pull-ups to its VIO join the bus, so the bus's pull-ups in parallel stay at or above about 2.2 kΩ | [proposed] |
| Wiring | One of two, chosen when building [user]: **Qwiic**, a Qwiic cable from the fan-out's Qwiic socket to the Notecarrier-X's Qwiic connector, its red 3.3 V wire unconnected; or **direct**, three wires soldered from the fan-out to the Notecarrier-X's 16-pin header pins `GND`, `SDA` and `SCL`, with `VIO` and every other header pin unconnected. With direct wiring the LiPo may also be soldered to the header's `VBAT` and `GND` instead of the JST-PH connector [proposed]. The firmware is the same for both | [user]; pins [proposed] |
| Power | The Notecarrier-X's own LiPo (or its USB-C) supplies the Notecard and its modem peaks (up to about 2 A), a LiPo of at least **2000 mAh** [proposed]: at hourly syncs (`cell_every` 60) the Notecard uses about **10 mAh a day** (Blues' published sync and idle figures; real use depends on signal, and each `sync:true` alert or email adds a session), so 2000 mAh lasts about 6 months between charges; either wiring joins GND, SDA and SCL only, the Qwiic cable's 3.3 V wire and the header's `VIO` left unconnected, so the board's 3.3 V and the Notecarrier's regulator are never tied together | [proposed] |
| ATTN | Not connected in the first build (the Qwiic cable carries no ATTN; the ESLOV connector and the header's `ATTN` pin would); the device polls the Notecard | [proposed] |
| Antenna | LTE: one flexible LTE antenna on the Notecard's `MAIN` u.FL, required because the Notecarrier-X has no onboard antenna, mounted with its long axis vertical on the inside of a plastic enclosure wall, at least 10 mm from metal, the LiPo and both PCBs, its coax kept clear of the AMOLED's QSPI lines and the ESP32-S3's antenna. GPS: only with `gps` `on` (FR-33.15), a GPS antenna on the Notecard's `GPS` u.FL, placed with the best view of the sky the site allows, for the fix during the setup check; with `gps` `off` the port stays empty. As shipped the Notecarrier-X puts no bias on `GPS`, so the antenna is passive (the GPS part of Blues' combined LTE + GPS flexible antenna, or a single flexible antenna). An active GPS antenna, for a faster or outdoor fix, needs the carrier's 0 Ω 0603 jumper between `VGPS_OUT` and `VGPS_IN` fitted (3.8 V on the centre conductor); with it fitted, no passive antenna is ever plugged into `GPS`, since one that is a DC short can damage the Notecard. WiFi: the Notecard's own WiFi is not used (FR-33.14), so its onboard WiFi antenna and `WIFI` u.FL need no placement. The combined LTE + GPS flexible antenna is used whole, never cut; separate orientations take two single flexible antennas. The fit is checked by the signal bars (`tbattr.qo`) with the enclosure closed | [proposed] |

### 33.3 Settings

Added to the §13.2 form and the BLE `settings` write (§26); all optional, so a
submission without them leaves the uplink as it was (`wifi` on a new device).

| Field | Format | Valid | Empty or absent |
|-------|--------|-------|-----------------|
| `uplink` | `wifi` or `cell` | one of the two; `cell` needs `nh_product`, given or stored | the stored value; `wifi` when none is stored [proposed] |
| `nh_product` | the Notehub ProductUID, e.g. `com.example.joe:weather` | 1 – 64 bytes, letters, digits, `.`, `:`, `-` and `_` | the stored value |
| `cell_every` | minutes between the Notecard's outbound syncs | whole minutes, 10 – 1440 | 60 [proposed] |
| `gps` | take a GPS fix of the place during the setup check (§24): `on` or `off` | one of the two; `on` needs `uplink` `cell`, given or stored | the stored value; `off` when none is stored [proposed] |
| `cell_mail` | email over cellular: `route` (a note to `mail.qo`, which a Notehub route passes to an HTTP mail service) or `off` (no email while `uplink` is `cell`) | one of the two | the stored value; `off` when none is stored [proposed] |

### 33.4 Notes

| Notefile | Holds | Sync | Notehub route (set up by the user, User Manual) |
|----------|-------|------|------------------------------------------------|
| `tb.qo` | One ThingsBoard batch: the telemetry array of §32.3 as `body.t`, at most 100 readings | at the next periodic sync | General HTTP, JSONata `body.t`, to `<tb_url>/api/v1/<tb_token>/telemetry` |
| `tbattr.qo` | The attributes of §32.3, with `"uplink":"cell"`, the signal bars, the Notecard's firmware version and, when a place is stored, `latitude` and `longitude` (FR-33.18) | at the next periodic sync | General HTTP to `<tb_url>/api/v1/<tb_token>/attributes` |
| `alert.qo` | One alert: `topic`, `title`, `priority`, `tags`, `message` of §31.3 | at once (`sync:true`) | General HTTP to the ntfy server's JSON publish endpoint `<ntfy_url>/` |
| `mail.qo` | One email: `to`, `subject`, `text` (the §25 message, the §28 result); only with `cell_mail` `route` | at once (`sync:true`) | General HTTP to the user's mail service (e.g. SMTP2GO or SendGrid), its API key in the route |

Secrets stay off the cellular path: the ThingsBoard token, the ntfy token and
any mail credentials live in the Notehub routes, never in a note.

### 33.5 Requirements

Settings:

- **FR-33.1** [Must] [user] The portal form (§13.2) and a BLE `settings`
  write (§26) shall carry the fields of §33.3. A submission shall be valid
  only if every field given is valid per §33.3 and, with `uplink` `cell`,
  `nh_product` is given or stored, else answer 400 naming the first invalid
  field (portal) or `invalid=<field>` (BLE).
- **FR-33.2** [Must] [derived] Valid settings shall be stored in NVS with the
  other settings at T3, or by an accepted BLE write in a key window (T14),
  reported by the `settings` read (FR-26.9) with `uplink=<wifi|cell>`, `cell_mail=<route|off>` and `gps=<on|off>`, and
  erased by a factory reset (T10), which also sets `uplink` back to `wifi`.
- **FR-33.13** [Must] [user] Email over cellular shall be selectable by
  `cell_mail`: with `route`, a due summary or test result is added to
  `mail.qo` (FR-33.6); with `off`, while `uplink` is `cell`, no email is due
  or sent, L25, L26 and L43 `mail` never appear, the mail settings (§25.2)
  stay stored, and the summary period still advances as after a dropped send
  so no backlog builds up. With `uplink` `wifi`, `cell_mail` has no effect.
- **FR-33.3** [Must] [user] With `uplink` `wifi` the device shall never
  install the Notecard's I2C device, and behave as without §33.

Notecard setup:

- **FR-33.4** [Must] [derived from §33.1] On each boot with `uplink` `cell`,
  the device shall check the Notecard with `card.version`, then, only when the
  Notecard's stored values differ, send `hub.set` with `product`
  `nh_product`, `mode` `periodic`, `outbound` `cell_every` and `inbound`
  1440. A Notecard that does not answer within **2 s** [proposed] is logged
  as L44 and that boot's sends are dropped as failed (FR-33.10).
- **FR-33.14** [Must] [user] On each boot with `uplink` `cell`, the device
  shall set `card.transport` to `cell` when the Notecard reports another
  transport, so the Notecard never scans for or joins a WiFi network and its
  default WiFi-first order is not used; it never sends `card.wifi`.
- **FR-33.5** [Must] [proposed] The device shall never cut the Notecard's
  power or reset it; the Notecard stays powered through deep sleep.

Sending:

- **FR-33.6** [Must] [user] With `uplink` `cell`, a due ThingsBoard upload
  (FR-32.4, FR-32.6), a due alert (§31) and a due summary or test result
  (§25, §28, when `cell_mail` is `route`, FR-33.13) shall be sent as notes of §33.4 instead of over WiFi; a timer
  wake shall never start WiFi (FR-21.13 keeps the panel dark and WiFi off),
  and a window still starts WiFi for the page, OTA and BLE when credentials
  are stored and `wifi` is `on` (§34).
- **FR-33.7** [Must] [user] For ThingsBoard, the upload mark (FR-32.8) shall
  move when the Notecard answers a `note.add` to `tb.qo` without an error,
  because the Notecard then holds the batch until Notehub has it. An upload
  carries every reading after the mark in notes of at most **100**
  readings [proposed], as FR-32.7. The commissioning program (Phase 15)
  confirms that a note of 100 readings is accepted by the Notecard and added
  within NFR-33.1's 10 s over the 100 kHz bus; if it is not, the limit is
  **50** [proposed].
- **FR-33.8** [Must] [user] Alerts and email shall be added with `sync:true`,
  so the Notecard connects at once; the device does not wait for that
  connection. An alert or email counts as sent (L39, L25) when the Notecard
  accepts the note; delivery is seen on Notehub and the target.
- **FR-33.9** [Must] [derived from §9.1] No note shall carry the WiFi
  password, the mail password, `tb_token` or `ntfy_token`.
- **FR-33.10** [Must] [derived] A `note.add` that fails (no answer within
  2 s, an `{err}` reply, a full Notefile) shall be logged as L44 and treated
  as that send's failure: a ThingsBoard batch stays after the mark (FR-32.11),
  an alert follows FR-31.6, an email follows §25's dropped send.
  Before that, any Notecard request answered with an `{io}` error (the
  Notecard could not read the request, so it did nothing) shall be sent
  again, at most **2** more times, **250 ms** apart [proposed], and count
  as failed only when the last answer is still one [user]. Any other
  answer is final.
- **FR-33.11** [Must] [derived] Each accepted note shall be logged as L43
  (§7.2).

Time:

- **FR-33.12** [Must] [derived from FR-22.1] With `uplink` `cell`, on a boot
  whose clock was never set (FR-22.6), the device shall ask the Notecard for
  the time (`card.time`) and, when it answers with a time, set the clock and
  mark readings synced as after an SNTP sync, logging L20 with `src=cell`.
  In a window, when the Notecard has no time yet (it learns it only in a
  session), the device shall request one `hub.sync` and ask `card.time`
  again every **5 s** for up to **60 s** [proposed]; on a timer wake it asks
  once and does not wait.
  Provisioning that stores `uplink` `cell` (T3, T16) goes on without a
  restart, so it shall run the Notecard setup (FR-33.4, FR-33.14) and this
  step as a window does, at once after storing [derived].

Location (optional, during the setup check):

- **FR-33.15** [Must] [user] With `uplink` `cell` and `gps` `on`, the device
  shall seek a GPS fix with the Notecard's own GPS during each setup check
  hold (§24) and during the `gps` test (FR-33.20), and only then. With `gps`
  `off`, on a timer wake, and outside the hold and the test, it never turns
  the Notecard's GPS on.
- **FR-33.16** [Must] [proposed] When the hold starts, after the Notecard
  setup (FR-33.4, FR-33.14) and the time step of FR-33.12, the device shall
  send `card.location.mode` `continuous` and ask `card.location` every
  **5 s** until it answers with a fix or the hold ends. A fix is a GPS
  location (`{gps}` in its `status`, with `lat`, `lon` and `time`) taken
  after the search began: its `time` later than the search's start when the
  clock is set, else later than the location the first answer held
  [derived]. A location the Notecard already held, from GPS or a cell
  tower, is never the search's fix.
  The Notecard turns its GPS on only after its first completed Notehub
  sync, and never while its modem is on, so the search may wait for a
  session to end (R-35).
- **FR-33.17** [Must] [proposed] On a fix, the device shall store the
  latitude and longitude (5 decimals) and the fix time in NVS, replacing
  any earlier place, send `card.location.mode` `fixed` with them (the GPS
  turns off, and Notehub tags each later event with the place), and log
  L45 with the fix. The fix does not end the hold.
- **FR-33.18** [Must] [proposed] When the hold ends without a fix (key or
  timeout), the device shall send `card.location.mode` `fixed` with the
  stored place, or `off` when none is stored, and log L45 `none`. A stored
  place shall be sent as the attributes `latitude` and `longitude` with the
  ThingsBoard attributes (`tbattr.qo`, and §32.3 over WiFi), and erased by
  a factory reset (T10).
- **FR-33.19** [Must] [proposed] During the hold with `gps` `on`, line 6
  shall show `GPS searching <mm>:<ss>` (time since the search began) until
  a fix, then `GPS <lat> <lon>` (5 decimals, at most 44 characters), in
  place of the add-on values, which return when the hold ends.
- **FR-33.20** [Must] [user] The `gps` test of §28 shall run the search of
  FR-33.16 – FR-33.19 outside the hold, without a factory reset, keeping the
  device awake as `quick` does (FR-28.6) and running after any other test.
  It passes at the first fix (FR-33.17) and fails when no fix comes within
  **20 min** [proposed] (the Notecard ends a seek after about 17 min, R-35),
  or at once when no search can start (`uplink` not `cell`, `gps` `off`, or
  the Notecard not set up); either way the search ends as in FR-33.18. A
  press of the user key ends it as failed with `by=key`, and a reset with
  `by=reset`, after which the next boot ends the search (FR-33.18). It
  counts as one planned reading in FR-28.11, taken when it found a fix, and
  logs L32 and L33 with `fix=<yes|no>` in place of `readings=` and
  `averages=` (§7.2).

Power and data:

- **NFR-33.1** [Must] [derived from NFR-32.1] A timer wake that sends notes
  shall be back asleep within **10 s** [proposed] of the wake, whether the
  Notecard accepts them or not.
- **NFR-33.2** [Should] [proposed] With the `std` setup (§28) and hourly
  ThingsBoard batches, the cellular data used shall stay under **4 MB a
  month**, so the 500 MB included lasts the 10 years. `tb.qo` uses a
  Notefile template (`note.template`) [proposed], and the commissioning
  program measures one hourly batch so the monthly figure is computed from
  a measurement, not an estimate.

### 33.6 Verification contracts

| ID | Precondition · stimulus | Expected observation | Must NOT happen | Tier |
|----|------------------------|----------------------|-----------------|------|
| FR-33.1 (core) | Bodies, nothing stored: none of the fields; `uplink=cell` alone; with `nh_product`; `uplink=lte`; `nh_product` with a space; `cell_every` 9, 1441, 60 | `wifi`; 400 `nh_product`; `cell`; 400 `uplink`; 400 `nh_product`; 400 `cell_every` twice, then valid | `cell` stored without a ProductUID | host |
| FR-33.3 (core) | `uplink_core` with `uplink` `wifi` at a due upload, alert and summary | Every send on WiFi; no Notecard call | A note added with `uplink` `wifi` | host |
| FR-33.3 | Shipped build, `uplink` `wifi`, a Notecard on the bus; reset and a timer wake | L43 and L44 never appear; the sends of §25, §31 as before | A Notecard line | target (R-31) |
| FR-33.4 (core) | `notecard` core builds `hub.set` from stored values; answers that match and that differ | No `hub.set` when they match; one with the four fields when they differ | `hub.set` on every boot | host |
| FR-33.14 (core) | `notecard` core with `card.transport` answers `wifi-cell`, `cell`, none | One `card.transport` `cell` for `wifi-cell` and for none; nothing for `cell` | `card.wifi` ever sent; a transport other than `cell` |
| FR-33.7 (core) | `uplink_core` over 250 readings after the mark; the second `note.add` refused; then all accepted | Notes of 100, 100, 50 in order; the mark at 100 after the refusal, at 250 afterwards | The mark moved on a refused note | host |
| FR-33.13 (core) | `uplink` `cell`, a summary due: `cell_mail` `route`; `off`; `off` then `route` at the next due time; `uplink` `wifi` with `off` | A `mail.qo` note; nothing due, the period advanced, mail settings kept; one note for the new period only; sent over WiFi as §25 | An email or note with `off` on cellular; a backlog of summaries | host |
| FR-33.8, FR-33.9 (core) | Alert `t_high` and a summary with every token and password stored | `alert.qo` and `mail.qo` bodies as §33.4 with `sync:true`; no secret in any body | A token or password in a note | host |
| FR-33.10 (core) | Answers: none, `{"err":"…"}`, `{"total":1}` | L44 `timeout`, L44 `err`, L43 | Success on an error reply | host |
| FR-33.10 (core) | An `{io}` error answer, then a good one; three `{io}` answers; a `{product-noexist}` error | Sent again, then success; three sends, then L44 `err`; one send, L44 `err` | A fourth send; a resend after any other answer | host |
| FR-33.6, FR-33.11, NFR-33.1 | Sleep test build, `uplink` `cell`, a Notecard on the bench (R-31), an upload due on the next timer wake | `web: wake cause=timer`, L21, L43 `tb.qo`, `web: sleep` within 10 s; the event on Notehub; no WiFi start | WiFi on a timer wake; the panel lit | target (R-31) |
| FR-33.12 (core) | Window, clock never set: `card.time` answers no time, then a time on the 4th ask; never a time; timer wake with no time | One `hub.sync`, asks at 5 s steps, clock set on the 4th; asks stop at 60 s; one ask, no wait | A second `hub.sync`; waiting on a timer wake | host |
| FR-33.12 | Power-on with `uplink` `cell`, the bench AP down | L20 with `src=cell` and a year ≥ 2026 within 60 s | A reading with `at=none` after L20 | target (R-31) |
| FR-33.12 | Erased flash, a Notecard that has the time; portal provisioning with `uplink=cell` and the bench AP without internet | L20 with `src=cell` within 60 s of the 200, before the first reading | The first reading with `at=none` | target (R-31) |
| FR-33.1, FR-33.15 (core) | Bodies: `gps=on` with `uplink` `wifi` stored; with `uplink=cell`; `gps=yes` | 400 `gps`; stored; 400 `gps` | `gps` `on` stored with `uplink` `wifi` |
| FR-33.15, FR-33.16, FR-33.17 (core) | Hold with `gps` `on`: `card.location` answers no fix twice, then a fix; `gps` `off`; a timer wake | `continuous` once, asks at 5 s steps, then `fixed` with the place, the place stored, L45; no location request with `off` or on a timer wake | A location request outside the hold; the hold ended by the fix |
| FR-33.16 (core) | `card.location` answers: the held location twice, then a newer GPS fix; with the clock set, a location older than the start, then a newer one; no location, then a fix; a newer cell tower location without `{gps}` | No fix, no fix, the fix; no fix, the fix; no fix, the fix; no fix | The held location taken as the fix |
| FR-33.18 (core) | Hold ends by key with no fix: a place stored; none stored | `fixed` with the stored place, L45 `none`; `off`, L45 `none` | The stored place erased by a failed search |
| FR-33.19 (core) | Line 6 at 0 s, 75 s, after a fix at 40.712776, -74.005974 | `GPS searching 00:00`; `GPS searching 01:15`; `GPS 40.71278 -74.00597` | More than 44 characters; the add-on values during the search |
| FR-33.15, FR-33.17 | A Notecard with its GPS antenna at a window (R-31, R-35), `gps` `on`, provisioned | L45 with the bench's place within the hold; `latitude` and `longitude` in the next `tbattr.qo` | L45 on a timer wake | bench (operator) |
| FR-33.20, FR-28.1, FR-28.12 (core) | `setup=std,gps`; `setup=gps` alone; `quick` then `gps` with a fix; `gps` with none; `gps` ended by a key | `std,gps`; 400 `setup`; `… id=gps fix=yes result=pass`, `Test pass 13/13`; `fix=no result=fail`, `Test FAIL 0/1`; `fix=no result=fail by=key` | `gps` before `quick` or `cycle`; a reading counted for `gps` | host |
| FR-33.20 | A Notecard with its GPS antenna at a window (R-31, R-35), `uplink` `cell`, `gps` `on`; BLE write `setup=std,gps` | L32 `id=gps`, line 6 `GPS searching <mm>:<ss>`, then L45 with the place and L33 `fix=yes result=pass` within 20 min, or L45 `none` and L33 `fix=no result=fail`; no sleep during the test | The search on a timer wake; a factory reset needed | bench (operator) |
| FR-33.8 | The user's routes set up, an alert and a summary due | The ntfy message and the email arrive | Nothing on the target with L43 logged | bench (operator) |
| NFR-33.2 | A month of the `std` setup on cellular | Notehub's data use for the device under 4 MB | — | bench (operator) |

---

## 34. WiFi Off

### 34.1 Purpose & peer

WiFi is optional. A device placed where no WiFi reaches, or whose owner does
not want it on a network, is set to `wifi` `off`: it never starts the
station, the SoftAP after provisioning, or any service that runs over WiFi,
and keeps everything else. It takes its readings, keeps its history and
sleeps as before (§21, §23, §27), sends its scheduled traffic as notes when
`uplink` is `cell` (§33), and is set up and read over BLE (§26, §27.3).
Bluetooth is unchanged: the provisioning window at first setup and the 3 s
key window afterwards. Peer: the person setting up the device, through the
portal (§13) at first setup or a BLE settings write (§26). The pure
`device_logic` decides the WIFI_OFF state (§5.3); the portal and
`ble_settings` cores check the field; the composition root leaves the WiFi
driver uninitialised.

With WiFi off the device loses the status page and its API (§14), HTTP OTA
(§15; firmware is then updated over USB, or after turning WiFi back on over
BLE), mDNS (§17), the UDP log (§16) and SNTP (§22).

### 34.2 Setting

Added to the §13.2 form, in the WiFi section before `ssid`, and to the BLE
`settings` write (§26); optional, so a submission without it leaves WiFi as
it was (`on` on a new device).

| Field | Format | Valid | Empty or absent |
|-------|--------|-------|-----------------|
| `wifi` | `on` or `off` | one of the two | the stored value; `on` when none is stored [user] |

### 34.3 Requirements

Settings:

- **FR-34.1** [Must] [user] The portal form (§13.2) and a BLE `settings`
  write (§26) shall carry `wifi`; a submission with any other value shall be
  answered 400 naming `wifi` (portal) or `invalid=wifi` (BLE), NVS unchanged.
- **FR-34.2** [Must] [user] With `wifi` `off` in a submission, `ssid`
  and `password` shall be optional: given, they are checked by FR-13.4 and
  stored; absent in PROVISIONING, no credentials are stored; absent in a key
  window, the stored ones are kept (FR-26.7). Stored credentials are never
  erased by turning WiFi off, so turning it back on needs no retyping.
- **FR-34.3** [Must] [derived from FR-26.9] A `settings` read shall report
  `wifi=<on|off>`, and the form description (FR-26.19) shall carry the field,
  so the BLE page draws it as the portal does (FR-26.21).

States:

- **FR-34.4** [Must] [user] At boot with `wifi` `off` stored, the device
  shall enter WIFI_OFF (T15) without starting the station or the SoftAP,
  whether or not credentials are stored.
- **FR-34.5** [Must] [user] When a valid submission with `wifi` `off`
  arrives in PROVISIONING, from the portal or BLE, the device shall store
  it, answer 200 or `result` `ok`, stop the SoftAP and the provisioning
  window, and enter WIFI_OFF without restarting (T16). Provisioning is then
  complete (FR-21.12) and the setup-check mark is set as at T3 (FR-24.1).
- **FR-34.6** [Must] [derived from FR-5.19] In WIFI_OFF the device shall
  leave the state only by a restart: a factory reset (T10) back to
  PROVISIONING, or an accepted key-window write (T14), after which it boots
  into CONNECTING when that write set `wifi` `on` with credentials given or
  stored.
- **FR-34.7** [Must] [derived] Entering WIFI_OFF shall log L2
  (`web: state=WIFI_OFF`), and the screen shall show the WIFI_OFF lines of
  §5.4.

Behaviour with WiFi off:

- **FR-34.8** [Must] [user] In WIFI_OFF the WiFi radio shall stay off in
  every boot, window and wake: no station, no SoftAP, no status server, OTA,
  mDNS, UDP log or SNTP, and nothing at the station or SoftAP MAC addresses
  in a bench scan. The one exception is ESP-NOW with `espnow_role` not `off`
  (§38): a node's frames (FR-38.10) and a gateway's receiving and replies,
  with no association and no access point [proposed]. The Notecard's own WiFi stays unused too (FR-33.14).
- **FR-34.9** [Must] [user] The window after a boot that is not a timer
  wake (FR-21.2, FR-21.3, FR-21.11) shall run in WIFI_OFF as with WiFi on but
  without WiFi: the panel with its lines, the setup check (§24), the BLE key
  window (§26), then the task and sleep (§21). A timer wake shall follow
  FR-21.13 without any of its WiFi exceptions.
- **FR-34.10** [Must] [user] With `wifi` `off` and `uplink` `cell`, the
  summary, alerts and ThingsBoard uploads shall be sent as notes as §33
  describes, unchanged.
- **FR-34.11** [Must] [user] With `wifi` `off` and `uplink` `wifi`,
  nothing shall be sent: no summary, test result email, alert or upload is
  due; the summary period advances as after a dropped send (as FR-33.13),
  alert conditions are still tracked (§31.3) but not sent, and the
  ThingsBoard mark (FR-32.8) stays, so the readings since it are uploaded once
  WiFi or cellular is back. The cellular uplink section's intro (§33.3) says
  so, in the portal and on the BLE page alike.
- **FR-34.12** [Must] [derived from FR-22.1] In WIFI_OFF the clock shall be
  set only from the Notecard (FR-33.12); with `uplink` `wifi`, readings after
  a power loss carry no time (FR-22.3, FR-22.6) until the clock is set.

### 34.4 Verification contracts

| ID | Precondition · stimulus | Expected observation | Must NOT happen | Tier |
|----|------------------------|----------------------|-----------------|------|
| FR-34.1, FR-34.2 (core) | Portal and `ble_settings` cores, nothing stored: `wifi=off` alone; `wifi=off` with a valid `ssid`; `wifi=off` with a 33-byte `ssid`; `wifi=no`; `ssid` alone; key window with credentials stored: `wifi=off` alone | valid, no credentials; valid, credentials kept; 400 `ssid`; 400 `wifi`; valid, `wifi` `on`; valid, credentials still stored | `wifi=off` refused for a missing `ssid`; stored credentials erased | host |
| T15, T16, T10, T14 (core) | Transition function: BOOT with `wifi` `off` stored, with and without credentials; PROVISIONING with a valid `wifi=off` submission; WIFI_OFF with factory reset and with BS | WIFI_OFF with no station or SoftAP action; WIFI_OFF with SoftAP stop; restart; restart | CONNECTING or PROVISIONING from WIFI_OFF without a restart | host |
| FR-34.11 (core) | `wifi` `off`, `uplink` `wifi`: a summary, an alert and an upload due | Nothing sent; the summary period advanced; the mark unchanged | Any send; WiFi started | host |
| FR-34.5, FR-34.7 | Erased flash, shipped build; bench `enter-portal` with `wifi=off` and no `ssid` | 200, then `web: state=WIFI_OFF` with no boot line between; the FR-13.1 SSID gone from a bench scan ≤ 5 s after the 200 | `web: state=CONNECTING`; a restart | bench |
| FR-34.4, FR-34.8 | Provisioned with `wifi=off` and the bench AP's credentials; USB reset; observe 120 s with the bench AP up | `web: state=WIFI_OFF` ≤ 3000 ms after reset; the bench AP's station list never shows the device; no FR-13.1 SSID in scans | An association with the bench AP; `web: online`; the SoftAP seen | bench |
| FR-34.9 | As FR-34.4, sleep test build, a summary due on the next timer wake | The window without WiFi, `web: sleep`, `web: wake cause=timer`, L21, `web: sleep`; no WiFi start | A WiFi start on any wake; the panel lit on the timer wake | target |
| FR-34.6, FR-34.3 | WIFI_OFF; a key window; the bench BLE central reads `settings`, then writes `wifi=on` and applies | The read holds `wifi=off`; `result` `ok`; a restart; `web: state=CONNECTING`, then `web: online` | ONLINE without a restart; PROVISIONING | bench |

---

## 35. Cellular Build

### 35.1 Purpose & peer

A second product build of this firmware runs on the Waveshare
ESP32-S3-SIM7670G-4G (ESP32-S3 with a SIMCom SIM7670G LTE Cat-1 modem with
GNSS, a MAX17048 fuel gauge and a solar charger) [user]. It has the features
of the T-Display build wherever the board allows (§35.3). Its cellular uplink
is the board's own modem, which carries an ordinary IP link over PPP: with
`uplink` `cell` the WiFi senders (§25, §31, §32) run unchanged over that link
and reach their servers directly, with no vendor cloud between the device and
its peers. The Blues Notecard of §33 is not part of this build. LTE Cat-1
offers no power saving mode, so the modem is powered down after each send and
attaches afresh at the next (FR-35.23). The board has no panel of its own, so
it drives an OLED, and, when the optional e-paper display of §36 is fitted,
the e-paper in mild weather and the OLED in the cold (FR-35.2); it takes an
off-board key (FR-35.3), and can read its settings from a microSD card
(FR-35.4).

Peers: the modem on UART 1; the cellular network; the senders' servers,
reached over the internet; the OLED, the optional e-paper display (§36), the
off-board key and the microSD card. The T-Display build is unchanged by this
section.

Its verification contracts are §35.6.

### 35.2 Hardware

The board facts below are read from Waveshare's V2 schematic
(`ESP32-S3-A-SIM7670X-4G-V2.pdf`, 15 Dec 2025) and checked by
`commissioning/ws-probe` on SLOT2.

| Item | Value | Source |
|------|-------|--------|
| Board | Waveshare ESP32-S3-SIM7670G-4G, the board the user owns. Its four switches (SW2): CAM off (the camera's rail off; GPIO 7 – 16, 39, 41, 42 and 46 are free of it), HUB on (the rail of the USB hub and the CH343 console), 4G off (the modem's supply then follows GPIO 21, below), USB on (the modem's USB on the board's hub, for AT debugging from a PC; off puts it on the ESP32-S3's GPIO 19/20) | [user]; switch functions [derived from the board] |
| MCU | ESP32-S3R8 (8 MB octal PSRAM in the package, which takes GPIO 33 – 37; PSRAM unused), 16 MB flash (PY25Q128HA) | [derived from the board] |
| Console | UART 0 (GPIO 43/44) through the board's CH343 USB-serial, which also flashes it (auto-program, no BOOT or EN wires) | [derived from the board] |
| Modem | SIM7670G on UART 1, 115200 baud, through the board's TXB0104 level shifter: the ESP32-S3 receives on GPIO 17 (the modem's TXD) and sends on GPIO 18 (its RXD); the modem's DTR on GPIO 45 and RI on GPIO 40 through the same shifter; RTS, CTS, DCD, RESET and STATUS not wired. The shifter's modem side runs from the modem's own 1.8 V (VDD_EXT), so GPIO 17/18 float until the modem has started. PWRKEY is held low by the board, so the modem starts by itself whenever its supply comes on. Its supply is the battery switched by the board's FDC6333C: with the 4G switch off, GPIO 21 high switches it on and low off, held low through deep sleep. The modem's start-up draw drops the board's USB console (CH343) off its host for about a second each time the supply comes on, so serial lines printed then are lost; the ESP32-S3 does not reset. A nano SIM whose plan covers LTE Cat-1, fitted before power-up | [derived from the board]; UART, GPIO 21 [user] |
| Power | One 18650 cell in the board's holder, required: the modem runs only from the cell (its transmit peaks of about 2 A), never from USB alone. Charged from USB-C or the solar input (CN3791); 3.3 V from the EA3036C. The cell's voltage and charge are read from the MAX17048 (0x36); the board's GPIO 1 divider (VBAT / 3) is not used | [derived from the board]; reading [user] |
| Sensor bus | The BME280 (0x76), the water probe's ADS1115 (0x48) and the board's MAX17048 (0x36) on I2C controller 0, SDA GPIO 15, SCL GPIO 16, 100 kHz, from the header. With the CAM switch off the board has no pull-ups on it (its 4.7 kΩ run from the camera's rail), so two 4.7 kΩ resistors from SDA and SCL to 3.3 V at the header serve the bus; the ESP32-S3's internal pull-ups alone carry the gauge's short reads but not a display's page writes | [user]; the board's pull-ups [derived from the board] |
| Loop bus | The loop's isolated ADS1115 (0x49) on I2C controller 1, SDA GPIO 39, SCL GPIO 41, its supply and the isolator's board side switched by GPIO 42, as §29 with the pins moved | [user] |
| RS232 | MAX3232 powered from GPIO 11, TX GPIO 46 (a strapping pin, driven only as an output, with the transceiver unpowered at reset), RX GPIO 2, on UART 2 because the modem holds UART 1 | [user] |
| Water probe | Switch line GPIO 14 | [user] |
| E-paper (optional, §36) | Waveshare's 4.2" e-Paper Module (V2, Rev 2.2): 400 × 300 black and white, an SSD1683 controller (the CrowPanel's controller, `docs/Method/project/crowpanel-epaper.md`), 3.3 V, 4-wire SPI on SPI2 shared with the OLED: VCC 3.3 V, GND, CLK GPIO 7, DIN GPIO 8, DC GPIO 10, CS GPIO 3, BUSY GPIO 1 (high while the controller works; the internal pull-down holds it low with no panel, FR-35.25), RST GPIO 20 [proposed]. GPIO 20 is the ESP32-S3's USB D+, free with the USB switch on, so the build turns its USB-Serial/JTAG console off | [user]; RST [proposed]; the console [derived] |
| OLED | A 3.12" 256 × 64 SSD1322 module (16 grey levels, e.g. Newhaven NHD-3.12-25664), 4-wire SPI on SPI2: SCK GPIO 7, MOSI GPIO 8, CS GPIO 9, DC GPIO 10, RES GPIO 19 [proposed]; a 10 kΩ resistor from CS to GND on the OLED's lead marks it plugged in (FR-35.25) [proposed]. The 3.12" OLED Ver 2.1 module the user owns ships set for 8080 parallel and shows no reset RC: for 4-wire SPI its R6 link moves to R5 (R5 and R8 fitted), and its unused parallel inputs go to GND, and its VCC comes from the board's 5 V (on 3.3 V the module's regulator leaves the panel dark) (`docs/Method/project/ws-displays.md`) | [user]; RES [proposed]; the module [derived from the board]; 5 V [derived from the bench] |
| Qwiic | The board has no Qwiic socket. The sensor bus (header P3: GPIO 15/16, 3.3 V, GND) goes by a made-up lead to a Qwiic star adapter (e.g. SparkFun Qwiic MultiPort), into which the BME280, on a Qwiic breakout, plugs; the lead is checked with a meter before first power-up (black on GND, red on 3.3 V, blue on GPIO 15, yellow on GPIO 16). Beside the header's 4.7 kΩ pull-ups, at most one board's stay in and the others' are cut, so the bus's pull-ups stay at or above about 2.2 kΩ. The loop (5 pins: GND, VSYS, GPIO 42, 39, 41), the water probe (6 pins: GND, VSYS, 3V3, GPIO 15, 16, 14) and RS232 (4 pins: GND, GPIO 11, 46, 2) keep JST-XH sockets as in §2.2; the OLED and the off-board key are not I2C and have their own connectors | [user]; pins [proposed] |
| Off-board key | A momentary, normally open, illuminated push button on a cable of at most 3 m: the key on GPIO 12 (an RTC GPIO), active low, a 10 kΩ pull-up to 3.3 V, 1 kΩ in series and 100 nF to GND at the board end and a TVS diode on the line; its LED on GPIO 13 through a resistor | [user] |
| BOOT key | GPIO 0 on the board, as §18 | [derived from the board] |
| microSD | The board's slot, SDMMC 1-bit, CMD GPIO 4, CLK GPIO 5, D0 GPIO 6 (no external pull-ups; the ESP32-S3's own are used); no card-detect line, so an empty slot is known by a mount that times out; FAT32; always powered (the board does not switch its supply) | [derived from the board]; FAT32 [user] |
| Antennas | LTE: the board's LTE antenna on the modem's main u.FL. GPS: an active antenna on the GNSS u.FL, which the board biases from 3.3 V, only with `gps` `on` | [user] |
| Pins | The header GPIOs are P3: 4 – 16, 46; P2: 0 – 3, 19 – 21, 39 – 45 (43/44 the console). Used: 7 – 14, 2, 11, 39, 41, 42, 46, and 15/16 shared with the MAX17048; GPIO 4 – 6 are the microSD's, 17/18, 21, 40 and 45 the modem's, 0 the BOOT key's, 38 the board's RGB LED (unused); GPIO 1, 3 and 20 are the e-paper's, GPIO 19 the OLED's reset [proposed]; no header GPIO is left free, so further sensors join the I2C buses | [derived from the rows above] |
| Bench slot | SLOT2 (CH343 serial 5B91031846), MAC 28:84:85:91:89:58 | [user]; MAC [derived from the board] |

### 35.3 Feature parity

| Feature | § | On this build |
|---------|---|---------------|
| Connectivity states, WiFi portal, status page and API, HTTP OTA, UDP log, mDNS | 5, 13 – 17 | Same, over the ESP32-S3's WiFi |
| BOOT key, factory reset | 18 | Same (GPIO 0) |
| Panel and its six lines | 5.4, 6 | The OLED (FR-35.16); with the optional e-paper (§36), e-paper or OLED by temperature (FR-35.2, FR-35.15); same line content |
| Serial log | 7 | Same, plus L46, L47 and L49 (§7.2), and L48 with the e-paper |
| Watchdog, identity, deep sleep, time | 19 – 22 | Same; the user key is the off-board key (FR-35.3); the clock also from the modem (FR-35.13) |
| Weather sensor, setup check, reading history, sampling setups | 23, 24, 27, 28 | Same; the LED of FR-24.9 is the off-board key's LED |
| Summary email, push alerts, ThingsBoard | 25, 31, 32 | Same, over WiFi or over the modem (FR-35.8) |
| BLE setup | 26 | Same, plus settings from microSD (FR-35.4) |
| 4-20 mA loop, RS232, water probe | 29, 30 | Same, on the pins of §35.2 |
| Cellular uplink: ThingsBoard batches and attributes, alerts, email, network clock, GPS place, `gps` test | 33 | Same behaviour through the modem instead of notes (FR-35.8 – FR-35.14); no Notehub, no routes |
| Notecard settings `nh_product`, `cell_every`; `card.*` and `hub.*` requests; Notefile templates | 33.3, 33.5 | Not on this build: sends go out when they are due, not at a Notecard sync |
| HTTP OTA over cellular | — | Not offered [user]: an image is about 1.4 MB of cellular data; OTA stays on WiFi, or USB |
| WiFi off | 34 | Same; with `uplink` `cell`, sends go through the modem |
| The satellite `day.qo` trial | — | Not product behaviour on either build (`commissioning/sat-probe`) |
| ESP-NOW link, node or gateway | 38 | Same; a gateway may also use `uplink` `cell`, the modem's PPP link (§38.2) |

### 35.4 Settings

On this build the §33.3 fields read as below, and `apn` and `cell_plmn` are
added to the §13.2 form and the BLE `settings` write (§26); `nh_product` and
`cell_every` are not offered, and a submission carrying them is answered as
for an unknown field.

| Field | Format | Valid | Empty or absent |
|-------|--------|-------|-----------------|
| `uplink` | `wifi` or `cell` (`cell` is the board's modem) | one of the two; `cell` needs `apn`, given or stored | the stored value; `wifi` when none is stored [user] |
| `apn` | the SIM provider's access point name, e.g. `hologram` | 1 – 63 bytes, letters, digits, `.` and `-` | the stored value |
| `cell_plmn` | a network to lock to, MCC+MNC, e.g. `310410`; empty for automatic choice | empty, or 5 – 6 digits | empty clears the lock (automatic choice); absent keeps the stored value, empty when none is stored [user] |
| `gps` | as §33.3 | as §33.3 | as §33.3 |
| `cell_mail` | email over cellular: `on` (sent by §25's SMTP over the modem) or `off` | one of the two | the stored value; `on` when none is stored [user] |
| `oled` | the SSD1322 OLED, when plugged in (FR-35.25): `on` uses it, `off` never does | one of the two | the stored value; `on` when none is stored [proposed] |

### 35.5 Requirements

Build:

- **FR-35.5** [Must] [user] The cellular build shall come from the same
  source tree as the T-Display build, the board chosen at build time; the
  T-Display build's image and behaviour do not change, and the cellular
  image fits a 4 MB application slot (C-10.7) with the same partition table,
  including the 2 MB `history` partition.
- **FR-35.6** [Must] [user] Every feature §35.3 lists as the same shall meet
  its own section on this build, with the same log lines; where §35.3 names a
  change, this section's requirement replaces the named part only.

Displays:

- **FR-35.2** [Must] [user] With the e-paper fitted (found by FR-35.25 with `epaper` `on`, §36), the
  build shall show the §5.4 lines on the e-paper display when the temperature is above **40 °F** (4.4 °C) and on the OLED
  below it. The choice is made when a window starts, from the latest kept
  reading (§23), and kept for the whole window; with a reading of 40.00 °F the
  OLED is used, and with no reading at all the e-paper [user]. Once on the OLED, the build
  returns to the e-paper only at **42 °F** or above [user] (2 °F of
  hysteresis, so a reading near 40 °F does not swap the display at each
  window). The display not chosen stays off: the OLED dark, the e-paper
  holding its last drawing. Without the e-paper, the OLED is used in every
  window; with the OLED not fitted (not found, or `oled` `off`, §35.4) and the e-paper fitted, the e-paper is
  used in every window whatever the temperature, and L48 says `epaper`
  [proposed]. With both off, nothing is drawn and the window runs on.
- **FR-35.15** [Must] [user] With the e-paper fitted, on it lines 1 – 5 shall be drawn
  in a 12 × 24 font (33 characters across 396 pixels) and line 6 in an
  8 × 16 font (44 characters), both generated by `bdf_to_c.py`. A window
  starts with a full refresh; a changed line is drawn with a partial refresh,
  at most once every **5 s**; at the end of the window the screen is drawn
  once more, with a full refresh, showing the last reading on line 4 and
  `Asleep, key to wake` on line 5 and the status block below the six lines
  (FR-35.24), and the controller is put in its deep sleep. A timer wake never
  draws on the e-paper (FR-21.13).
- **FR-35.24** [Should] [user] Below the six lines, the e-paper shall keep a
  **status block** for development: up to 10 rows of the 8 × 16 font (44
  characters, the rest cut) filling the panel's lower 160 pixels, drawn only
  in the window's final full refresh, so it adds no refresh [proposed]. Its
  rows, kept in RTC memory so a timer wake's results show at the next window
  [proposed]: the firmware version and the reset reason; the window's wake
  cause and its local time; the latest modem attach (`attach <ms> ms csq <n>
  <network>`, the failed step of L47, or `modem off`); the latest result of
  each send path that ran (ThingsBoard, alert, mail); the latest settings-card
  result (L49); the cell's voltage and charge (MAX17048); and the latest four
  `web:` lines at level W or E, newest first [proposed].
- **FR-35.25** [Must] [user] When a window starts, before FR-35.2 chooses,
  the build shall detect which displays are plugged in, so either one, both or
  neither may be fitted with no setting changed [user]. The OLED (with `oled`
  `on`) is found when its CS, GPIO 9, reads low with the internal pull-up on,
  through the 10 kΩ to GND on its lead; CS is then driven high until the panel
  starts [proposed]. The e-paper (with `epaper` `on`) is found when BUSY,
  GPIO 1 with the internal pull-down on, rises within 2 ms of the software reset
  that follows its hardware reset [proposed]; an e-paper found but not chosen is
  put back in its deep sleep at once, so its drawing stays. A display counts as
  fitted when found with its setting `on`; a setting `off` skips the detection.
  L50 reports the result each window. Displays are plugged in or out with the
  power off: one changed while a window runs is seen at the next window.
- **FR-35.16** [Must] [user] On the OLED, the six lines shall be drawn in
  a 6 × 10 font generated by `bdf_to_c.py`, 42 characters a line across 252
  of its 256 pixels, so lines 1 – 5 (at most 33) fit whole and line 6 is cut
  at 42. The OLED is turned off (its display-off command) at the end of
  every window and stays off on a timer wake.
- **FR-35.17** [Must] [derived from FR-5.27] A failure of the chosen display
  shall be reported to the state machine as an LCD failure is (FR-5.27), and
  the device shall carry on without it; the other display is not tried in
  its place within that window.

Keys and LED:

- **FR-35.3** [Must] [user] The off-board key of §35.2 shall be the user key
  of every section that names it (§21, §24, §26, §28): it wakes the device
  from deep sleep, ends the setup check hold, opens the BLE key window with a
  3 s hold, and starts `quick` with a double press, with the same timings.
- **FR-35.18** [Must] [user] The off-board key's LED shall do what the
  LED of FR-24.9 does on the T-Display: flash at 1 Hz during the setup check
  hold, lit in a window while the display is on, off in sleep.

Settings from microSD:

- **FR-35.4** [Must] [user] On each boot that is not a timer wake, the device
  shall look on the microSD card for a file `/settings.txt` holding a
  settings body in the form of the BLE `settings` write (§26), one
  `field=value` a line, each value as typed (not percent-encoded; the device
  encodes it), empty lines and lines starting with `#` skipped. A file found shall be validated and applied as a BLE
  `apply` of that body is in the state the device is in: in PROVISIONING as
  FR-26.6, otherwise as FR-26.7, so absent secrets keep the stored ones and an
  accepted body restarts the device (T14).
- **FR-35.19** [Must] [user] After an accepted file, the device shall
  rename it to `/settings.done` with the values of `password`,
  `mail_password`, `tb_token` and `ntfy_token` removed, so the card carries
  no secret once applied and the same file is not applied again; after a
  refused file it leaves the file in place. Either way it writes
  `/settings.result` holding `ok` or `invalid=<field>` and logs L49.
- **FR-35.20** [Must] [user] With no card, an unreadable card or no
  `/settings.txt`, the device shall go on as without this feature, logging
  L49 `none` or `failed` (an empty slot shows as a mount that times out,
  the slot having no card-detect line); the card is unmounted before the window ends,
  and never mounted on a timer wake.

Modem and sending:

- **FR-35.23** [Must] [user] After the sends of a boot, a window or a wake
  are done, failed or given up, and after a GPS search with no send due, the
  device shall power the modem down (`AT+CPOF`) and then cut its supply
  (GPIO 21 low, held through deep sleep), so it draws nothing until the next
  send, which switches the supply on and attaches afresh. On each attach it logs the attach time, the network
  and the signal as L46.
- **FR-35.7** [Must] [user] The modem's supply shall be switched on only for
  a due ThingsBoard upload, alert or email with `uplink` `cell`, for the time
  step of FR-35.13, and for a GPS search (FR-35.14); with `uplink` `wifi` it
  stays powered off in every boot, window and wake.
- **FR-35.8** [Must] [user] With `uplink` `cell`, a due ThingsBoard upload
  (FR-32.4, FR-32.6) with its attributes, a due alert (§31) and a due summary
  or test result (§25, §28, with `cell_mail` `on`) shall be sent by their own
  sections' senders over PPP through the modem instead of over WiFi, with the
  same requests, bodies, success rules, upload mark and logs (L25, L26,
  L39 – L42). A timer wake never starts WiFi; a window still starts WiFi for
  the page, OTA and BLE when credentials are stored and `wifi` is `on`
  (§34). With `cell_mail` `off`, email over cellular follows FR-33.13's `off`.
- **FR-35.9** [Must] [user] A modem that does not register within
  **60 s** of being powered on, or a PPP link that does not come up
  within **10 s** [user] of registering, shall be logged as L47 and treated as each
  due send's failure as its section says (the ThingsBoard readings stay after
  the mark, an alert follows FR-31.6, an email follows §25's dropped send);
  the modem is then powered down (FR-35.23).
- **FR-35.10** [Must] [user] With `uplink` `cell`, the ThingsBoard
  attributes (§32.3) shall add `"uplink":"cell"`, the signal (CSQ), the
  modem's firmware revision, the network (MCC+MNC), the battery voltage in
  mV and its charge in % (`battery_pct`, from the MAX17048) and, when a place
  is stored, `latitude` and `longitude`.
- **FR-35.11** [Must] [user] With `cell_plmn` set, the device shall lock
  the modem to that network on each full attach (`AT+COPS=1,2`, no
  fallback); with it empty, automatic choice (`AT+COPS=0`), undoing an
  earlier lock.
- **FR-35.12** [Must] [derived from §9.1] The modem link shall carry nothing
  the WiFi path does not: the same TLS and certificate rules for `https://`
  peers and for the mail server, and no setting is sent to any peer other than
  its own.
- **FR-35.21** [Must] [user] A modem call that can wait longer than
  FR-19.1's 10 s (a power-on, an attach, a GNSS start) shall take the
  application task off the watch for that call only, and put it back when the
  call returns; FR-35.9's limits bound each such call. The window's display
  and keys wait for the call.
- **FR-35.22** [Should] [user] Until the SSD1322 is fitted, a bench build
  (`sdkconfig.standin`, never released) shall draw the six lines on a 1.3"
  128 × 64 SH1106 or SSD1306 I2C OLED on the sensor bus (address 0x3C), in
  the 6 × 10 font, 21 characters a line, the rest cut [user]. Everything
  else is as in the product build; the panel is turned on, off and reported
  as FR-35.16 and FR-35.17 say of the OLED.
- **NFR-35.1** [Must] [user] A timer wake that sends shall be back asleep
  within **60 s** of the wake when the attach succeeds, and within **75 s**
  [proposed] when it fails (FR-35.9's 60 s and 10 s limits, then the
  power-down). On this build FR-32.12's 40 s counts from the PPP link coming
  up.
- **NFR-35.2** [Should] [user] With the `std` setup (§28) and hourly
  ThingsBoard uploads, the cellular data used shall stay under **10 MB a
  month**, measured over a month on the bench.
- **NFR-35.3** [Should] [proposed] Asleep between windows with the modem's
  supply cut, the board's current from the battery shall be measured at
  commissioning, with and without a card in the slot, and recorded here and
  in the User Manual; the figure becomes this requirement's limit once the
  user accepts it.

Time:

- **FR-35.13** [Must] [derived from FR-33.12] With `uplink` `cell`, on a boot
  whose clock was never set, the device shall take the time from the modem's
  network time (`AT+CCLK?`) after registering and, when the network supplies
  none, by SNTP over PPP; it sets the clock and marks readings synced as after
  an SNTP sync, logging L20 with `src=cell`. In a window this attach is made
  even with no send due; on a timer wake it is made only with a send due.

Location:

- **FR-35.14** [Must] [derived from FR-33.15 – FR-33.20] With `uplink`
  `cell` and `gps` `on`, the setup check hold and the `gps` test shall seek a
  fix as FR-33.15 – FR-33.20 say, with the modem's GNSS in place of the
  Notecard's: the modem's GNSS is powered for the search (`AT+CGNSSPWR=1`)
  and off after it, LTE is stopped for the search (`AT+CFUN=0`), the device
  asks `AT+CGNSSINFO` every **5 s**, and a fix is an answer with a fix and a
  UTC time
  later than the search's start. The place is stored and logged (L45),
  shown on line 6 and sent with the attributes as §33 says; no `card.location`
  request exists on this build. After the search LTE is restarted only when a
  send is due; otherwise the modem is powered down (FR-35.23).

### 35.6 Verification contracts

Bench rows run on SLOT2 with the Waveshare board (R-31 for coverage at the bench);
`(core)` rows drive the pure cores on the host.

| ID | Precondition · stimulus | Expected observation | Must NOT happen | Tier |
|----|------------------------|----------------------|-----------------|------|
| FR-35.5 | Both builds from one commit | Each builds; the cellular image under 4 MB with the same partition table; the T-Display image's size and `sdkconfig` unchanged from a build without this section | One build's sources edited to build the other | host (build) |
| FR-35.4, FR-35.6 (core) | `ble_settings` core with §35.4 bodies, nothing stored: `uplink=cell` alone; with `apn=hologram`; `apn` of 64 bytes; `cell_plmn=31041`; `cell_plmn=3104100`; `cell_mail=route`; `nh_product=x` | 400 `apn`; valid; 400 `apn`; valid; 400 `cell_plmn`; 400 `cell_mail`; 400 `nh_product` | `cell` stored without an APN | host |
| FR-35.4, FR-35.20 (core) | Settings-card core: a valid file in PROVISIONING; in a key window without `password`; a file with `ssid` of 33 bytes; no file; an empty slot (the mount times out); an unreadable card | Applied as FR-26.6; applied as FR-26.7 keeping the stored password, restart; `invalid=ssid`, nothing stored; L49 `none`; L49 `none`; L49 `failed` | A refused file stored in part; the card mounted on a timer wake | host |
| FR-35.19 (core) | An accepted file holding `password`, `mail_password`, `tb_token`, `ntfy_token`; a refused one | `/settings.done` written with those four values empty and `/settings.txt` gone, `/settings.result` `ok`; `/settings.txt` kept, `/settings.result` `invalid=<field>` | A secret in `/settings.done`; the same file applied twice | host |
| FR-35.4, FR-35.19 | Operator: a card with a valid `/settings.txt` in the slot, power-on | L49 `ok`, a restart, the settings in a BLE `settings` read; the card on a PC holds `settings.done` without secrets and `settings.result` `ok` | The file applied on the next boot again | bench (operator) |
| FR-35.2 (core) | Display choice with the e-paper fitted: readings 45.00, 40.01, 40.00, 39.99 °F; none, also after the OLED; on the OLED, 41.99 then 42.00 °F; `epaper` `off` at 70 °F | e-paper, e-paper, OLED, OLED; e-paper; OLED, e-paper; OLED | A switch inside a window; the e-paper with `epaper` `off` | host |
| FR-35.2 (core) | `oled` `off` with the e-paper fitted at 30.00 °F and with no reading; both off | e-paper, e-paper; neither | The OLED driven with `oled` `off` | host |
| FR-35.15, FR-35.24 (core) | E-paper layout of the ONLINE lines, a line 6 of 50 characters and a status block of 12 rows, one of 60 characters | Lines 1 – 5 in 12 × 24 cells, line 6 its first 44 characters in 8 × 16, then 10 rows of 44 at y 140; the final screen with `Asleep, key to wake` on line 5 | A row drawn past the panel's 300 pixels; the status block in a partial refresh | host |
| FR-35.15, FR-35.24 | Operator: the e-paper fitted, `oled` `off`, power-on; the hold ended by the key; then a timer wake with an upload due and the next key wake | The §5.4 lines on the e-paper in the window, partial updates at most every 5 s; after `web: sleep` the sleep screen with the status block; the timer wake leaves the screen as it was; the next window's sleep screen shows that wake's attach and send | A drawing on a timer wake; a refresh after the controller's deep sleep | bench (operator) |
| FR-35.16 (core) | OLED layout of the ONLINE lines and a line 6 of 44 characters | Six rows of 6 × 10 cells, lines 1 – 5 whole, line 6 its first 42 characters | A line wrapped onto the next row | host |
| FR-35.22 (core) | Stand-in layout of the ONLINE lines on a stale screen; the init of each chip; a box past the panel | Six rows of 6 × 10 cells from x 1, each line cut at 21, only the boxes cleared; the display left off, page addressing, the SH1106 from column 2; refused | A line wrapped onto the next row; the display on before a window | host |
| FR-35.22 | Operator: the stand-in build on the Waveshare board with the 1.3" OLED on the sensor bus, power-on; the off-board key pressed in the hold | The §5.4 lines on the 1.3" OLED in the window, cut at 21, dark after `web: sleep`; the hold ends; the BME280 still read | A reset; lines mirrored or upside down; a sensor-bus error | bench (operator) |
| FR-35.16, FR-35.17 | Operator: power-on with the OLED fitted; then with its CS line unplugged | The §5.4 lines on the OLED in the window, dark after `web: sleep`; with CS unplugged the window runs on, the display failure logged | A reset or a halted window | bench (operator) |
| FR-35.3, FR-35.18 | Operator: asleep 300 s, presses the off-board key; then mark pending, power-on | `web: wake cause=button` ≤ 3 s after the press; during the hold the key's LED flashes 1 Hz and a press ends it with L24 `by=key` | Waking only at the period's end; the LED dark during the hold | bench (operator) |
| FR-35.23 (core) | Modem steps: a send done; a send failed; a GPS search with nothing due; `AT+CPOF` unanswered | `AT+CPOF` then the supply cut after each; the supply cut after the unanswered `AT+CPOF`; L46 on each attach | The supply left on after the boot's last send or search | host |
| FR-35.23, FR-35.7 | `uplink` `cell`, an upload due each timer wake | L46 on every run; between runs GPIO 21 held low and the modem's NET LED dark | The modem's supply on in sleep | bench |
| FR-35.25 (core) | L50 for each pair of states; the settings' defaults on a new board | `displays epaper=found oled=none` and the rest; `epaper=on&oled=on` | A state named wrongly | host |
| FR-35.25 | Windows with the e-paper plugged in, then unplugged (power off between), the OLED's lead with and without its 10 kΩ | L50 `found` then `none` for each; with the e-paper gone the OLED drawn whatever the temperature | A missing display chosen, or a present one missed | bench |
| FR-35.7 (core) | Modem wake decision: `uplink` `wifi` at a due upload; `cell` with nothing due on a timer wake; `cell` with an alert due; a window with the clock never set | No wake; no wake; wake; wake | The modem woken with `uplink` `wifi` | host |
| FR-35.8, NFR-35.1 | Sleep test build, `uplink` `cell`, the user's ThingsBoard and ntfy, an upload and an alert due on the next timer wake | L41 `what=telemetry`, L39, `web: sleep` within 60 s of the wake; no WiFi start | WiFi on a timer wake; the panel lit; the mark moved without L41 | bench |
| FR-35.9 (core) | Attach steps: registered at 59 s; not at 60 s; PPP up 9 s after registering; not within 10 s; with an upload, an alert and a summary due | Sends; L47 `register`; sends; L47 `ppp`; on each failure the mark kept, the alert as FR-31.6, the summary period advanced; modem off | A send after L47 | host |
| FR-35.21 | In a window, an alert due over the modem with the LTE antenna removed | L47 `register` about 60 s after the attach began, no `TASK_WDT` restart, the window running again after the call; then the hang build (variant `app`) still restarts with `TASK_WDT` | A watchdog restart during the attach; the task left unwatched after it | bench (operator) |
| FR-35.9 | The LTE antenna removed, an upload due on a timer wake | L47 `register` within 60 s of the wake, `web: sleep` within 75 s; the readings uploaded with the antenna back | A hang past 75 s | bench (operator) |
| FR-35.10 (core) | Attributes with `uplink` `cell`, CSQ 21, a SIM7670G firmware revision, network 310410, 3950 mV, 81 %, a place stored | The §32.3 attributes plus those keys, `battery_pct` and `latitude`, `longitude` | A secret or a key with no value | host |
| FR-35.11 (core) | `cell_plmn` `310410`; then empty after a lock | `AT+COPS=1,2,"310410"` on the full attach; `AT+COPS=0` | A lock with `cell_plmn` empty | host |
| FR-35.12 | `https://` ThingsBoard with a wrong certificate, over the modem | L42 `cert` as over WiFi | The request sent without a valid certificate | bench |
| NFR-35.2 | A month of the `std` setup with hourly uploads on cellular | The SIM provider's data use for the device under 10 MB | — | bench (operator) |
| NFR-35.3 | Operator: the build asleep between windows, `uplink` `cell`, the modem's supply cut; a meter in the battery lead; without, then with a card | Both currents recorded in this section and the User Manual | The modem's current in sleep | bench (operator) |
| FR-35.13 (core) | Clock never set: `AT+CCLK?` answers a network time; answers none, SNTP answers; timer wake with nothing due | Clock set, L20 `src=cell`; clock set by SNTP, L20 `src=cell`; no attach | An attach on a timer wake for the time alone | host |
| FR-35.13 | Power-on with `uplink` `cell`, the bench AP down | L20 `src=cell` with a year ≥ 2026 within 60 s | A reading with `at=none` after L20 | bench |
| FR-35.14 (core) | GNSS answers during a hold: no fix twice, then a fix with a UTC time after the start; a fix older than the start; the hold ends with none, a place stored | `AT+CFUN=0` and `AT+CGNSSPWR=1` once, asks at 5 s steps, the place stored, L45, GNSS off and the modem powered down; no fix; L45 `none`, the place kept | LTE and GNSS on together; GNSS on after the search | host |
| FR-35.14, FR-33.20 | GPS antenna at a window, `gps` `on`; BLE write `setup=std,gps` | L32 `id=gps`, line 6 `GPS searching <mm>:<ss>`, then L45 with the place and L33 `fix=yes result=pass` within 20 min, or L45 `none` and `fix=no` | The search on a timer wake | bench (operator) |

---

## 36. E-paper Display (optional)

### 36.1 Purpose & peer

Both builds can carry an e-paper display, which keeps its last screen with no
power. It is optional on both [user]: the T-Display build shows its lines on
the AMOLED and the cellular build on the OLED (§35), and each is complete
without an e-paper. On the cellular build it is developed now, before the
SSD1322 arrives; on the T-Display build after that build's other phases
(Phase 18, FR-36.3) [user]. Peer: the e-paper panel, a 4.2" 400 × 300
SSD1683 module on SPI as the CrowPanel bring-up measured
(`docs/Method/project/crowpanel-epaper.md`).

The cellular build's e-paper rules are FR-35.2, FR-35.15 and
FR-35.24; the T-Display build's (its pins, and what the e-paper shows beside
the AMOLED) are defined when Phase 18 starts.

### 36.2 Setting

Added to the §13.2 form and the BLE `settings` write (§26) when Phase 18 is
built; optional, so a submission without it leaves the e-paper as it was.

| Field | Format | Valid | Empty or absent |
|-------|--------|-------|-----------------|
| `epaper` | the e-paper display, when plugged in (FR-35.25): `on` uses it, `off` never does | one of the two | the stored value; `on` when none is stored [proposed] |

### 36.3 Requirements

- **FR-36.1** [Must] [user] The e-paper display shall be optional in both
  builds: with `epaper` `off`, or with no panel fitted, every other
  requirement is met as without this section, and nothing is drawn, driven or
  logged for the e-paper, apart from FR-35.25's detection with `epaper` `on`
  and its L50.
- **FR-36.3** [Must] [user] On the cellular build the e-paper display shall
  be built in Phase 18 beside Phase 17, before the SSD1322 OLED; on the
  T-Display build after every other phase of that build. No other phase
  depends on it.

---

## 37. Packed Messages

### 37.1 Purpose & peer

Links that carry only a few hundred bytes or less per message, a satellite
link (Blues Notecard in NTN mode over Skylo, at most 256 bytes a packet, a
larger one dropped without notice) and LoRa (LoRaWAN US915 carries 11 bytes
at SF10 and 53 at SF9), send the readings, alerts and attributes as **packed
messages**: one family of small binary messages that every such link uses
alike [proposed]. Each message is turned back into JSON where it leaves the
radio link (a Notehub route, or a LoRaWAN network server's payload
formatter), and from there into the very requests of §32.3 and the alert of
§31.3, so ThingsBoard and ntfy see the same keys and values whichever link a
reading came over. A new small-payload link then needs only its own
transport; the messages, their packer and their decoder are shared.

IP links keep their JSON: WiFi (§25, §31, §32) and the cellular build's PPP link
(§35) send §32.3 unchanged, where TLS, not the body, is most of the data
(`docs/Method/project/lte-m-modem.md`: 7 – 12 kB a run against about 100
bytes a reading), and the Notecard's cellular notes (§33.4) stay as they
are [proposed].

This section defines the messages only. Which link sends which message, and
when, belongs to the section that adds the link; until one is added, neither
build sends a packed message. The satellite trial (`commissioning/sat-probe`)
keeps its own 57-byte `day.qo` until it is moved to the `day` message of
§37.3.

Peers: the transport of the link (the Notecard on I2C, a LoRa radio); the
edge decoder (a Notehub route's JSONata, a LoRaWAN payload formatter in
JavaScript); ThingsBoard and ntfy behind it. Built on a shared pure `pack`
component in esp-idf-components (no ESP-IDF headers, so the host tier
compiles it) with a reference decoder in Python, `tools/pack_ref.py`, against
which the packer and every edge decoder are tested [proposed].

### 37.2 Encodings

A message is a list of named fields, each with a fixed binary type (§37.3).
It is sent in one of two encodings, which decode to the same JSON **body**:

| Encoding | Used on | What goes over the radio | Decoded at the edge by |
|----------|---------|--------------------------|------------------------|
| Template | Notecard links (NTN, and cellular where a link section chooses it) | A Notefile with a `compact` `note.template` whose body names each field with its Notecard type hint (§37.3, last column) and a fixed `port`; the Notecard packs the body itself | Notehub, which hands the route the body as JSON |
| Raw | Links with no Notecard (LoRa) | The fields' bytes in table order, little-endian, no padding; a field that is an array is its elements in order; the records of `rd` one after another | The payload formatter, which returns the body as JSON |

Notehub leaves a field whose value is 0 out of the body, so every decoder
reads an absent field as 0.

### 37.3 Messages

Every message starts with `v`, one byte: the format version in the top 3 bits
(1 for this section) and the message type in the low 5 bits. Times are UTC
seconds since 1970. Values use the units §32.3 shows, scaled to integers, so
a decoded value is the one the history CSV (§27.1) and ThingsBoard show:

| Quantity | Field type | Unit | Decoded as |
|----------|------------|------|------------|
| Temperature | int16 | 0.01 °F | `temp_f`, 2 decimals |
| Humidity | uint16 | 0.1 % | `rh_pct`, 1 decimal |
| Pressure | uint16 | 0.01 mbar above 500 mbar (500.00 – 1155.35) | `p_mbar`, 2 decimals |
| Add-on (§29.2) | float32 | the add-on's own unit | `<rs_name>` or `<loop_name>`, its decimals |

**`rd`, readings** (type 1; Notefile `prd.qo`, port 101) [proposed]: a batch
of readings, oldest first, as many as fit the link's message size.

| Field | Type | Holds | Hint |
|-------|------|-------|------|
| `v` | uint8 | version 1, type 1 | 21 |
| `t0` | uint32 | time of the first reading | 24 |
| `add` | uint8 | bit 0: slot 1 present in every record; bit 1: slot 2 | 21 |
| `n` | uint8 | records, 1 – 255 | 21 |
| per record: `dt` | uint16 | seconds after `t0` (so a batch spans at most 18 h) | [22] |
| `tf` | int16 | temperature | [12] |
| `rh` | uint16 | humidity | [22] |
| `p` | uint16 | pressure | [22] |
| `st` | uint8 | bit 0: synced (FR-22.3); bit 1: the BME280 reading failed, `tf`, `rh` and `p` 0 and not shown | [21] |
| `a1`, `a2` | float32 | slot 1 and slot 2 values, only when `add` says so; NaN for no value | [14.1] |

Size: 7 bytes plus 9 a record, plus 4 a record per add-on present, so 256
bytes hold 27 records with no add-on, 19 with one and 15 with two; 53 bytes
hold 5, 3 and 2. A reading taken with the clock never set (FR-22.6) is not
packed, as in §32.

**`day`, day averages** (type 2; Notefile `pday.qo`, port 102) [proposed]:
one quantity's 24 hourly averages over the 24 whole UTC hours before the
send, as the trial's `day.qo` with the quantity named.

| Field | Type | Holds | Hint |
|-------|------|-------|------|
| `v` | uint8 | version 1, type 2 | 21 |
| `t0` | uint32 | start of hour 1 | 24 |
| `step` | uint16 | 3600 | 22 |
| `q` | uint8 | 0 temperature, 1 humidity, 2 pressure, 3 slot 1, 4 slot 2 | 21 |
| `miss` | uint24 | bit i−1 set: hour i has no value | 23 |
| `a` | 24 × uint16 | each hour's average in the unit of the quantity (§37.3 table) for `q` 0 – 2, as float16 bits for `q` 3 – 4; 0 where missing | [22] |

Size: 59 bytes. Decoded into 24 §32.3 telemetry entries named for `q`, the
missing hours left out.

**`al`, alert** (type 3; Notefile `pal.qo`, port 103, sent at once) [proposed]:

| Field | Type | Holds | Hint |
|-------|------|-------|------|
| `v` | uint8 | version 1, type 3 | 21 |
| `t` | uint32 | time of the reading that started or ended it | 24 |
| `ev` | uint8 | 1 `t_high`, 2 `t_low`, 3 `rh_high`, 4 `rh_low`, 5 `p_high`, 6 `p_low`, 7 `sensor`, 8 `rs232`, 9 `loop`, 10 `history`, 11 `test` (§31.3) | 21 |
| `end` | uint8 | 0 the condition starts (or `test FAIL`), 1 it ends (or `test pass`) | 21 |
| `val` | uint16 | for events 1 – 6 the reading's value, in the quantity's unit and type (two's complement for temperature); else 0 | 22 |
| `lim` | uint16 | for events 1 – 6 the limit crossed, likewise; else 0 | 22 |

Size: 11 bytes, so an alert fits LoRaWAN's smallest payload. Decoded into
the ntfy JSON of §31.3 with its title, priority and tags; the body of a limit
alert is §31.3's, while a failure says `<what> failed` or `<what> ok` and a
test only `test pass` or `test FAIL`, the log lines they come from not being
sent.

**`st`, status** (type 4; Notefile `pst.qo`, port 104) [proposed]: the
§32.3 attributes that are not known at the edge.

| Field | Type | Holds | Hint |
|-------|------|-------|------|
| `v` | uint8 | version 1, type 4 | 21 |
| `fw` | 3 × uint8 | firmware major, minor, patch | [21] |
| `set` | uint8 | the normal setup in use, its row in §28.1 counted from 0 | 21 |
| `sig` | int8 | signal: RSSI in dBm, or the Notecard's bars | 11 |
| `mv` | uint16 | battery mV, 0 where the board does not measure it | 22 |

Size: 9 bytes. The host name is the device's name at the edge (the Notehub
device name, the LoRaWAN device ID), set to the FR-20.1 name when the device
is added there; `ip` is not sent.

No email goes over a packed link: the §25 message is kilobytes. A route may
turn a `day` message into a short email.

### 37.4 Requirements

- **FR-37.1** [Must] [proposed] Every message a small-payload link sends
  shall be one of §37.3, in the encoding of §37.2 the link names; no other
  layout is sent over such a link.
- **FR-37.2** [Must] [proposed] Every message shall start with `v`; a decoder
  given a version or type it does not know shall pass nothing on and log the
  message as unknown, never guess at its fields.
- **FR-37.3** [Must] [proposed] A change to any message's fields shall be a
  new version; decoders keep decoding every earlier version still on a device.
- **FR-37.4** [Must] [proposed] Decoded, a message shall give the keys and
  values of §32.3 (and §31.3 for `al`) that the same reading or alert gives
  over WiFi, within the decimals of §32.3.
- **FR-37.5** [Must] [proposed] The pure `pack` packer shall be told the
  link's largest message and shall put into one `rd` the oldest unsent
  readings that fit, never more bytes than that; a reading is marked sent
  only when the link's transport accepts the message, as FR-32.8.
- **FR-37.6** [Must] [proposed] A value outside its field's range shall be
  clamped to the range and its record still sent; an add-on value with no
  value is NaN; a failed BME280 reading is sent with `st` bit 1.
- **FR-37.7** [Must] [proposed] No secret and no text the edge can build
  shall be in a message: tokens, URLs, host name, titles and bodies stay at
  the edge (as §33.4).
- **C-37.1** [Must] [proposed] No message shall exceed 256 bytes in either
  encoding (the Skylo limit), whatever the link.
- **NFR-37.1** [Must] [proposed] `tools/pack_ref.py` shall define the
  decoding; the C packer and each edge decoder (Notehub JSONata, LoRaWAN
  formatter) are held to it on the same test vectors in the host tier.

### 37.5 Verification contracts

| ID | Precondition · stimulus | Expected observation | Must NOT happen | Tier |
|----|------------------------|----------------------|-----------------|------|
| FR-37.1, C-37.1 (core) | Pack each type at its largest: `rd` at 256 and 53 bytes with 0, 1 and 2 add-ons; `day`; `al`; `st` | Sizes 7 + n × (9 + 4k) with n 27/19/15 and 5/3/2; 59; 11; 9 | A message over its limit | host |
| FR-37.4, NFR-37.1 (core) | Readings at the sensor's limits, synced and unsynced, a failed BME280, NaN add-ons; packed raw and as a template body; decoded by `pack_ref.py`, the route's JSONata and the formatter | The §32.3 telemetry the WiFi path builds for the same readings, key for key | A value differing in its shown decimals | host |
| FR-37.4 (core) | A body with every zero field left out, as Notehub delivers it | The same decoding as with the zeros | A missing key or an error | host |
| FR-37.2, FR-37.3 (core) | Messages with version 0, 2, 7 and type 0, 5, 31 | Nothing passed on; logged as unknown | A decoded value | host |
| FR-37.5 (core) | 250 unsent readings, a 256-byte link, one add-on; the transport refuses the second message | Batches of 19, oldest first; the mark at 19 after the refusal | A reading skipped or sent out of order | host |
| FR-37.6 (core) | 400 °F, −400 °F, 1200 mbar, 450 mbar, 105 % | Clamped to each field's limits; the record sent | A wrapped value | host |
| FR-37.7 (core) | Every message type packed with tokens and URLs stored | None of them in the bytes | A secret in a message | host |

---

## 39. Hardware Clock (optional)

### 39.1 Purpose & peer

Both builds can carry a battery-backed real-time clock (RTC), so readings
keep their date and time where no network ever reaches, such as a mine shaft,
and after a power loss [user]. The RTC only carries a time forward: it holds
one after a network sync (§22, §33, §35) or the phone's clock (FR-39.5) set
it, and the network sources correct it whenever they are up. It counts in
UTC; the time zone stays FR-22.2. Peer: a Micro Crystal RV-8803-C7,
temperature compensated (±1.5 ppm from 0 to 50 °C, ±3 ppm from −40 to 85 °C,
about a minute a year), with a coin cell that runs it while the board is
unpowered.

### 39.2 Hardware

| Item | Value | Provenance |
|------|-------|------------|
| Part | The Adafruit RV-8803 breakout (product 6538, STEMMA QT / Qwiic); any RV-8803-C7 board on the bus works the same | [user] |
| Bus | Each build's sensor bus (I2C controller 0 at 100 kHz: GPIO 43/44 on the T-Display build, GPIO 15/16 on the cellular build), at the chip's fixed address 0x32, plugged into the Qwiic star (§2.2, §35.2); no other device on either build uses 0x32 | [user] |
| Backup | A CR1220 coin cell in the breakout's holder; 240 nA at 3 V | [derived from the board] |
| Pull-ups and LED | The breakout's I2C pull-ups cut as the Qwiic rows of §2.2 and §35.2 say; a power LED, if the board has one, cut by its jumper | [user] |

### 39.3 Requirements

- **FR-39.1** [Must] [user] The RTC shall be optional on both builds: with
  nothing answering at 0x32, every other requirement is met as without this
  section, and nothing is logged for it.
- **FR-39.2** [Must] [user] On every boot, before the first reading and before
  the time steps of FR-33.12 and FR-35.13, the device shall read the RTC and,
  when it holds a valid time (its V2F flag and RESET bit clear, a valid date, at or after
  2024-01-01), set the clock from it to 1/100 s. That counts as a sync for
  FR-22.3 and FR-22.4: readings taken after it are marked synced. L20 with
  `src=rtc` is logged when the clock was not synced before (a power-on or a
  reset), not on a timer wake.
- **FR-39.3** [Must] [user] With the clock set by FR-39.2, the time steps
  that wait for a clock never set do not run: no `hub.sync` and no 60 s of
  `card.time` asks (FR-33.12), and no attach for the time alone (FR-35.13).
- **FR-39.4** [Must] [derived] After each network sync (SNTP, FR-22.1; the
  Notecard, FR-33.12 and FR-39.6; the modem, FR-35.13 and FR-39.7) and each
  phone time taken (FR-39.5), the device shall write the clock's time into the
  RTC at the start of a second, which clears its flags. On a boot whose clock
  is synced while the RTC holds no valid time (a new RTC, or a cell changed
  with the board powered), it shall write it then.
- **FR-39.5** [Must] [user] The BLE setup page shall send the phone's clock
  (`control` `time=<ms since 1970 UTC>`, §26) right after pairing, in any
  window. The device shall take it when its clock is not synced, or differs
  from the phone's by more than **2 s** [user]: it sets the clock, marks it synced,
  writes the RTC if one is fitted and logs L20 with `src=phone`. A time before 2024 is
  ignored.
- **FR-39.6** [Must] [user] On the T-Display build with `uplink` `cell` and the clock
  set by FR-39.2, in each window, after the Notecard setup, the device shall
  ask `hub.sync.status` and `card.time` once each, without a `hub.sync` or a
  wait. When the Notecard's last completed sync is at most **3600 s** old
  (`completed`), its time counts as a network sync: the clock set, L20 with
  `src=cell`, the RTC written (FR-39.4). An older time is not taken, because
  the Notecard's own clock has drifted since.
- **FR-39.7** [Must] [derived from FR-39.4] On the cellular build with an RTC, each attach
  that registers shall also take the network time (`AT+CCLK?`) and, when the
  network supplies one, count it as a network sync as FR-35.13 does.
- **FR-39.8** [Should] [user] At each network sync with a valid RTC,
  before writing it, the device shall measure the RTC's error against the
  network time and log L55 with the days since the RTC was last set from a
  network. When that measurement is precise to half a trim step, its
  source's resolution over the elapsed time at most **0.12 ppm** (SNTP: 50 ms, about
  5 days; the Notecard and the modem: 1 s, about 97 days), and at least
  **7 days** have passed, it shall add the trim that takes out the measured rate
  (0.2384 ppm steps, within −32 … +31) and log the old one as `was`. A phone
  time sets the RTC but starts no measurement: the next network sync after
  it logs no L55.
- **FR-39.9** [Must] [derived] L56 `rtc lost` shall be logged when the RTC
  answers with no valid time (its cell ran out with the board unpowered, which
  sets V2F or leaves the time frozen with RESET set, or a new RTC) on each boot that is not a timer wake; L56 `rtc failed` when it
  answered at 0x32 but a transfer then failed.
- **FR-39.10** [Must] [derived] The RTC shall be used only from the
  application task, which owns the sensor bus's other traffic.
- **NFR-39.1** [Must] [user] The fitted RTC shall run within **±3 ppm** of
  the bench clock at bench temperature over 24 h (the commissioning probe).

### 39.4 Verification contracts

| ID | Precondition · stimulus | Expected observation | Must NOT happen | Tier |
|----|------------------------|----------------------|-----------------|------|
| FR-39.2 (core) | `rv8803` core: registers for 2000-01-01, a leap day, 2099-12-31, with 1/100 s; 30 February; a second 60; BCD out of range | The times, to 10 ms; no time for the invalid ones | A 30 February taken as a time | host |
| FR-39.2, FR-39.9 (core) | `rv8803` core: flag and control registers with V2F, V1F only, RESET, neither | Stale; valid; stale; valid | A frozen time (RESET set) taken as valid | host |
| FR-39.2, FR-39.3 (core) | Boot choice: RTC valid on a power-on; valid on a timer wake; lost; absent; valid before 2024 | Clock set, L20 `rtc`; clock set, no L20; L56 `lost` (not on a timer wake), clock unset; nothing; L56 `lost` | L20 on a timer wake; a time before 2024 taken | host |
| FR-39.5 (core) | Phone time with the clock unsynced; synced and 1.5 s apart; synced and 3 s apart; a time before 2024 | Taken; ignored; taken; ignored | A phone time taken within 2 s of a synced clock | host |
| FR-39.6 (core) | `hub.sync.status` `completed` 120 s; 3600 s; 3601 s; absent | Taken; taken; not taken; not taken | A time from a Notecard that last synced over an hour ago | host |
| FR-39.8 (core) | Network sync: 2 s fast after 30 days by SNTP; 1 s fast after 30 days by the Notecard; 100 ms after 3 days by SNTP; after a phone time; trim at +31 and still fast | L55 and the trim raised by 3 (a positive step slows the RTC); L55, trim kept; L55, trim kept; no L55; trim kept at the limit | A trim from a measurement coarser than half a step | host |
| FR-39.5 (page) | The BLE page connecting to a simulated device | One `control` `time=<now in ms>` after the first reads | No time sent; a second | host |
| FR-39.2, FR-39.3 | Shipped build, RTC fitted and set by an SNTP sync; bench AP stopped; a reset (a power cycle where the bench has one) | L20 `src=rtc`; L21 readings with `at=` a time, `synced=1` | `at=none`; a `hub.sync` or 60 s wait | target |
| FR-39.1 | Shipped build with no RTC | Every log as before the phase; no L55, L56 | An `rtc` line | target |
| FR-39.4, FR-39.8 | RTC fitted; ONLINE; SNTP syncs | L55 at each sync after the first (no trim under 7 days) | L55 before any network sync | bench |
| FR-39.5 | Phone with the BLE page, clock unsynced (bench AP down, RTC lost) | L20 `src=phone`; readings with a time | No L20 | bench (operator) |
| NFR-39.1 | Commissioning probe over 24 h against the bench capture clock | Rate within ±3 ppm | A rate outside | bench (operator) |

---

## 38. ESP-NOW Link

### 38.1 Purpose & peer

ESP-NOW is an optional third uplink on both builds, off by default [user].
A battery **node** sends its readings, alerts and status on each timer wake
as packed messages (§37) in ESP-NOW frames to a **gateway**, another device
of either build that stays awake and passes them on over its own uplink, so
ThingsBoard and ntfy see each node as they would see a device on WiFi
[user]. Either build can be a node or a gateway; the role is a setting, not a
separate image [user]. A node keeps its history, panel, setup check and BLE
setup; ESP-NOW changes only how its data leaves the board. A gateway is still
a weather station of its own.

ESP-NOW runs on the ESP32-S3's own 2.4 GHz radio, the one WiFi and BLE use;
it needs no association with an access point. Espressif's long range (LR)
mode is offered as a setting on both roles [user], off by default until a
range test on these boards shows it is worth its airtime [user].

Peers: the other devices of the ESP-NOW network, identified by their station
MAC; for a gateway, ThingsBoard through its MQTT gateway API (one ThingsBoard
device per node [user]) and the ntfy server of §31. The evaluation that
measures acknowledgement rate, range, airtime, wake-to-sleep time and frame
loss during a BLE window is `docs/Method/project/espnow.md`, written by the
Phase 19 commissioning probe. Built on a shared `espnow_link` component (L0,
in esp-idf-components) and an app component `espnow` whose pure core
`espnow_core` holds the field rules, the framing, the replay window, the
backlog and the gateway's forward decisions [proposed]; the record packer and
unpacker are the shared `pack` component of §37.

### 38.2 Settings

Added to the §13.2 form and the BLE `settings` write (§26) on both builds;
all optional, so a submission without them leaves ESP-NOW as it was (off on
a new device).

| Field | Format | Valid | Empty or absent |
|-------|--------|-------|-----------------|
| `uplink` | adds `espnow` to `wifi` and `cell` (§33.3, §35.4) | `espnow` needs `espnow_role` `node`, `espnow_gw` and `espnow_key`, given or stored | the stored value |
| `espnow_role` | `off`, `node` or `gateway` | one of the three; `node` needs `uplink` `espnow`; `gateway` needs `uplink` `wifi` (with `wifi` `on`), or `cell` on the cellular build, and `espnow_key` | the stored value; `off` when none is stored [user] |
| `espnow_gw` | the gateway's station MAC, `aa:bb:cc:dd:ee:ff` (a node only) | 6 hex bytes, not broadcast or multicast | the stored value |
| `espnow_key` | the network key, 32 hex characters | exactly 16 bytes | the stored value (in a key-window BLE write an absent key keeps the stored one, as for the other secrets) |
| `espnow_ch` | the channel, 1 – 13; empty means search (node) or channel 1 (gateway on cellular) | empty or 1 – 13 | the stored value; empty when none is stored [proposed] |
| `espnow_lr` | long range mode, `on` or `off` | one of the two | the stored value; `off` when none is stored [user] |

A gateway on the T-Display build with `uplink` `cell` (the Notecard) is not
offered [proposed]: the gateway API is an MQTT connection, which the
Notecard does not carry (§33.1).

### 38.3 Frames

Every frame is at most 250 bytes, the ESP-NOW v1 limit, which every peer and
both LR rates carry [proposed]:

| Part | Bytes | Holds |
|------|-------|-------|
| `h` | 1 | frame version in the top 3 bits (1), kind in the low 5: 1 data (node to gateway), 2 reply (gateway to node) |
| `ctr` | 4 | the sender's frame counter, uint32 little-endian |
| body | data: 1 – 225; reply: 4 | data: one §37 message in the raw encoding (§37.2): `rd`, `al` or `st`; reply: the gateway's UTC time in seconds, uint32, 0 with its clock never set |
| `tag` | 8 | AES-128-CCM tag under `espnow_key` over `h`, `ctr`, the node's MAC and the body; the body is encrypted |

In both directions `ctr` is the node's counter and the CCM nonce is the
node's MAC (6 bytes), `ctr` (4), `h` (1) and two zero bytes, so no nonce
repeats under one key while a node's counter only rises; a reply that is sent
again for a repeated frame is the same bytes (FR-38.16). The
§37 packer is told 225 bytes as the link's largest message (FR-37.5), so an
`rd` holds up to 24 readings with no add-on, 16 with one and 12 with two
[derived from §37.3].

### 38.4 Requirements

Settings:

- **FR-38.1** [Must] [user] The portal form (§13.2) and a BLE `settings`
  write (§26) shall carry the fields of §38.2 on both builds. A submission
  shall be valid only if every field given is valid per §38.2 and the
  combined stored-and-given values meet its "Valid" column, else answer 400
  naming the first invalid field (portal) or `invalid=<field>` (BLE).
- **FR-38.2** [Must] [derived from §9.1, FR-9.3] The device shall not output
  `espnow_key` on serial, UDP, any HTTP response, any email, any BLE read or
  any ESP-NOW frame; the `settings` read (FR-26.9) reports only
  `espnow_key_set=<0|1>`.
- **FR-38.3** [Must] [proposed] Valid settings shall be stored in NVS with the
  other settings at T3, T16 or an accepted BLE write (T14), reported by the
  `settings` read, and erased with the frame counter and the gateway's node
  table by a factory reset (T10).
- **FR-38.4** [Must] [user] With `espnow_role` `off` nothing of this section
  shall run: the ESP-NOW driver is never initialised and the device behaves
  exactly as without this section.

Radio:

- **FR-38.5** [Must] [proposed] A gateway with `uplink` `wifi` shall use its
  access point's channel; a gateway on cellular shall use `espnow_ch`, or
  channel 1 when it is empty. The gateway shall show its station MAC and its
  channel on `GET /`, in `/api/status` (`espnow_mac`, `espnow_ch`), in the
  BLE `info` read and on line 6 during a window, so they can be copied into
  each node.
- **FR-38.6** [Must] [proposed] A node with `espnow_ch` given shall send on
  it. With it empty, a node shall try channels 1 – 13 in turn until a reply
  arrives, keep the channel found in RTC memory and NVS, and search again
  after **3** wakes in a row with no reply.
- **FR-38.7** [Must] [user] With `espnow_lr` `on`, a node shall send at the
  LR rate (250 kbit/s by default; 512 kbit/s as a build option) with its
  radio in LR-only mode, and a gateway shall run its station in 802.11 b/g/n
  plus LR mode, so it still joins an ordinary access point and hears both LR
  and normal nodes [proposed]. With `espnow_lr` `off`, frames go at the
  normal rate and the protocol mask is unchanged.
- **FR-38.8** [Must] [proposed] A gateway shall keep its station's power save
  off (`WIFI_PS_NONE`), so no frame is missed while the station dozes, and
  shall keep receiving during a BLE window, accepting that a node may need a
  retry.
- **FR-38.9** [Must] [proposed] A node shall never send while a BLE window is
  open (as FR-26.5) or while its station is associated: on a boot with a
  window and `wifi` `on`, it sends after the station has left its access
  point (FR-21.7).

Node:

- **FR-38.10** [Must] [user] With `uplink` `espnow`, after each reading the
  node shall send the readings after its send mark as `rd` messages, oldest
  first, at most **5** frames a wake [proposed], an `al` for each alert due
  (§31.3) before them, and an `st` on each boot with a window. On a timer
  wake this is the fifth exception of FR-21.13: the WiFi driver starts in
  station mode without joining a network, sends, stops, and the device
  sleeps; the panel stays dark.
- **FR-38.11** [Must] [proposed] A frame counts as delivered only when a
  reply arrives within **50 ms** whose tag checks and whose `ctr` is the one
  sent; a frame is tried up to **3** times. The send mark (as FR-32.8) moves
  past the readings of an `rd` only when it is delivered, so readings that
  missed the gateway go with a later wake while the history holds them.
- **FR-38.12** [Must] [proposed] The frame counter shall rise by 1 with each
  frame sent, kept in RTC memory and written to NVS every 256 frames; at boot
  the node continues from the NVS value plus 256, so no counter is ever used
  twice under one key.
- **FR-38.13** [Must] [proposed] A node whose clock was never set (FR-22.6),
  or differs from a reply's time by more than 2 s, shall set its clock from
  the reply's time when it is not 0 and log L20 with `src=espnow`. Readings
  taken before the first set carry no time and are not sent (§37.3).
- **FR-38.14** [Must] [user] With `uplink` `espnow`, no summary email is due
  or sent (the period advances as after a dropped send, as FR-33.13), and the
  node's own ThingsBoard and ntfy settings are unused; its alert conditions
  are tracked as in §31.3 and sent as `al`.

Gateway:

- **FR-38.15** [Must] [user] A gateway shall not enter deep sleep. Where §21
  would sleep for N s it shall stay awake for N s with the panel dark and
  ESP-NOW receiving, then run the task as on a timer wake but with its uplink
  kept up; a key press lights the panel and opens a key window as a key wake
  does (FR-21.11), without a restart.
- **FR-38.16** [Must] [user] A gateway shall accept a data frame only if its
  version and kind are known, its tag checks under `espnow_key`, and its `ctr`
  is above the last one accepted from that MAC; it shall then send a reply
  carrying the same `ctr` and its UTC time. A frame whose `ctr` equals the
  last one accepted from that MAC (a retry whose reply was lost) shall get the
  same reply bytes again and is not passed on a second time. Any other frame
  is dropped with L60 and gets no reply. The last counter of each node is kept in NVS, for at
  most **16** nodes [proposed]; a seventeenth node's frames are dropped
  (`full`).
- **FR-38.17** [Must] [user] A gateway shall pass an accepted `rd` and `st` on
  to ThingsBoard through its MQTT gateway API, as the telemetry and
  attributes of the device named by the node's FR-20.1 hostname (built from
  the frame's source MAC), with the keys and values of §32.3 (FR-37.4). It
  connects to the host of its `tb_url` on port 1883, or 8883 with TLS for an
  `https://` URL, with its own `tb_token`, which belongs to a ThingsBoard
  device marked as a gateway [proposed]. It shall reply to the node only after
  ThingsBoard's MQTT broker has acknowledged the publish (QoS 1), so a reading
  is never acknowledged and lost [proposed].
- **FR-38.18** [Must] [user] A gateway shall pass an accepted `al` on to its
  own ntfy server and topic (§31) as the alert §37.3 decodes, titled with the
  node's hostname; with ntfy off or not configured on the gateway, the alert
  is still acknowledged and only logged [proposed].
- **FR-38.19** [Must] [user] A gateway shall keep its own readings, uploads,
  alerts and summary exactly as without this section.

Logs:

- **FR-38.20** [Must] [derived] The device shall log L57 – L61 (§7.2) for the
  events they name, and never the key or a frame's body.

### 38.5 Verification contracts

| ID | Precondition · stimulus | Expected observation | Must NOT happen | Tier |
|----|------------------------|----------------------|-----------------|------|
| FR-38.1, FR-38.2 (core) | Portal and `ble_settings` cores: every row of §38.2 valid and one past each limit; `uplink=espnow` without `espnow_role=node`; `espnow_role=gateway` with `uplink=cell` on each build; a `settings` read with a key stored | Valid as §38.2; 400 naming the field; refused on the T-Display build only; `espnow_key_set=1` and no key | A key in any output; an inconsistent pair stored | host |
| §38.3, FR-38.16 (core) | `espnow_core` framing against `tools/pack_ref.py` (frame and CCM added): data and reply frames; a flipped bit in each part; the last `ctr` repeated; a lower `ctr`; version 2; a seventeenth node | Same bytes both ways; the stored reply resent and nothing passed on; dropped as `tag`, `replay`, `version`, `full` | A frame accepted after any change | host |
| FR-38.6 (core) | Channel search: the gateway on 1, 6, 11; then moved from 6 to 11 | Found in ≤ 13 tries; a search after 3 missed wakes, then 11 kept | A search on every wake | host |
| FR-38.10, FR-38.11 (core) | 120 readings after the mark, one add-on; the second frame gets no reply on any try | 5 frames planned; the mark after the first `rd` (16 readings); 3 tries of the second | The mark moved for an unanswered frame | host |
| FR-38.12 (core) | Counter at 1000 in RTC, 768 in NVS; a power loss | Next frame `ctr` 1024 (768 + 256) | A counter reused | host |
| FR-38.4 | Shipped build, `espnow_role` `off`; the JRN-01 journey | Passes unchanged; no ESP-NOW frame in a peer sniff for 10 min | Any ESP-NOW frame | bench |
| FR-38.10, FR-21.13 | SLOT1 node (`wifi` `off`, sleep test build) with the bench peer as gateway | `web: wake cause=timer`, L21, L57 `what=rd`, `web: sleep` within the commissioned limit; no join | WiFi association; the panel lit; a send in a key window | target |
| FR-38.13 | Node with the clock never set; the peer replies with a time | L20 `src=espnow` after the first reply; the next `rd` holds the reading | An unset clock after a reply | target |
| FR-38.16, FR-38.17 | SLOT1 as gateway on the bench AP; the peer sends `rd`, `st`, then replays one | L59; the node device's telemetry and attributes on the bench ThingsBoard as §32.3; L60 `replay` | A replayed frame forwarded or acknowledged | bench |
| FR-38.18 | As above; the peer sends a `t_high` `al` | An ntfy message titled with the peer's hostname | A reply before the publish was acknowledged | bench |
| FR-38.7 | SLOT1 in LR as node; the peer as a gateway in LR, then in normal mode; then SLOT1 normal, the peer LR | Delivered; not delivered; delivered | An LR node heard by a normal gateway | bench |
| FR-38.7 (range) | Two boards outdoors, line of sight, 100 m, 300 m and 1 km, normal and LR at 250 kbit/s | The delivered share at each distance recorded in `espnow.md` | — | bench (operator) |
| FR-38.15 | SLOT1 as gateway, sampling `std` | No `web: sleep` for 2 h; a reading at each period; a key press opens a window without L1 | A deep sleep; a restart on the key | target |

---

## 40. Low-Power Build (FeatherS3)

### 40.1 Purpose & peers

A third product build of this firmware runs on the Unexpected Maker FeatherS3
(ESP32-S3) [user], for a station that sleeps on a battery at a far lower current
than the T-Display build (measured at 380 µA alone and 451 µA with the sensor bus,
`docs/Method/project/sleep-current.md`). Its board keeps nothing but the clock's
bus powered in deep sleep: the sensors, the displays and the field inputs are
each on a supply the firmware switches off (FR-40.3 – FR-40.6). It has the
features of the T-Display build wherever the board allows (§40.3). It draws
the §5.4 lines on the e-paper display and the 3.12" OLED of the cellular
build, chosen by temperature as §35 says (FR-40.7). Its field inputs, a 4-20 mA
loop, a 0 – 5 V level probe and an ultrasonic level sensor, enter through a
surge protector and an isolation barrier, because their cables run 15 – 100 ft
over open ground [user] (C-40.5 – C-40.8).

Peers: the Blues Notecard of §33 on its own LiPo, the BME280, the RV-8803
(§39), the e-paper display, the OLED, the 4-20 mA transmitter (§29), the
0 – 5 V level probe (FR-40.9), the ultrasonic sensor on the serial port
(FR-40.12), the user key, and the testbench's RST and BOOT lines (C-40.9).
The T-Display build and the cellular build are unchanged by this section.

The first field probes, in the order they are tested [user]:

| Order | Probe | Input on this build |
|-------|-------|---------------------|
| 1 | MaxBotix MB7569-100 (SCXL-MaxSonar-WRM) ultrasonic sensor, 300 – 4999 mm, 2.7 – 5.5 V, about 34 mA at 3.3 V and 68 mA at 5 V, serial 9600 8N1 `R<four digits mm><CR>` at inverted logic levels | The isolated serial port, §30 add-on slot 1 (FR-40.12) |
| 2 | Earu EALLT-1 submersible level transmitter, 0 – 5 m, 0 – 5 V output, 10 – 30 V supply, vented cable (no barometric correction); its supply current is not published and is measured at commissioning | The analog board's voltage channel, §29.2 slot 2 with `loop` `volt` (FR-40.9) |
| 3 | A level transmitter with a 4-20 mA output and a 24 V supply | The analog board's loop channel, §29 unchanged, slot 2 with `loop` `in` |

Its verification contracts are §40.6.

### 40.2 Hardware

The board facts below come from Unexpected Maker's FeatherS3 datasheet and are
checked by the commissioning program of Phase 21 (R-44).

| Item | Value | Source |
|------|-------|--------|
| Board | Unexpected Maker FeatherS3 (P7 or later: its two STEMMA QT sockets on separate buses) | [user]; P7 [derived from the board] |
| MCU | ESP32-S3, 16 MB flash, 8 MB QSPI PSRAM (unused) | [derived from the board] |
| Console | The ESP32-S3's native USB-Serial/JTAG, which also flashes it and resets it into download mode | [derived from the board] |
| Power | One LiPo on the board's JST socket, charged from USB-C by the board's charger; two 3.3 V LDOs: LDO1 always on, LDO2 enabled by GPIO 39 and off in deep sleep. The battery voltage on GPIO 2 (the board's divider), USB present on GPIO 34 | [derived from the board] |
| Charging | Two cells, kept separate: the FeatherS3's LiPo and the Notecarrier-X's LiPo, each charged by its own board from that board's USB-C port. In the field, one 5 V source of at least 3 A (a charger, a USB battery pack or a solar controller with a USB output) feeds both USB-C ports through a power-only Y cable. On the bench, the FeatherS3's USB-C carries the bench's data cable, which also charges it, and the Notecarrier-X has its own power-only cable from the 5 V source. Nothing is wired to either board's header for charging (C-40.11) | [user] |
| Always-on bus | The horizontal STEMMA QT socket, I2C controller 0, SDA GPIO 8, SCL GPIO 9, 100 kHz, powered from LDO1: the RV-8803 (0x32, §39) and the Notecarrier-X (§33, its own LiPo, its red 3.3 V wire unconnected) | [user]; pins [derived from the board] |
| Switched bus | The vertical STEMMA QT socket, I2C controller 1, SDA GPIO 16, SCL GPIO 15 (the order of the board's Arduino and CircuitPython definitions), 100 kHz, powered from LDO2: the BME280 (0x76), and through the analog board's ISO1540 the isolated ADS1115 (0x49) | [user]; the pin order [user] |
| Display supply | A Pololu Mini Pushbutton Power Switch LV (#2808), input from LDO1's 3.3 V; its CTRL from GPIO 17: high on, low off, then left floating, which the switch's latch holds. Its output feeds the e-paper's VCC directly and the OLED's VCC through a 5 V step-up (Pololu U1V11F5), since the OLED module's own regulator leaves the panel dark on 3.3 V (`docs/Method/project/ws-displays.md`) | [user]; GPIO 17 [user]; the step-up [user] |
| Display bus | SPI2, SCK GPIO 36, MOSI GPIO 35, shared by both displays | [user] |
| E-paper | Waveshare 4.2" e-Paper Module (V2), SSD1683, as §35.2: CS GPIO 5, DC GPIO 6, RST GPIO 7, BUSY GPIO 10 (internal pull-down, FR-35.25) | [user]; pins [user] |
| OLED | 3.12" 256 × 64 SSD1322 module, as §35.2: CS GPIO 11 (10 kΩ to GND on its lead, FR-35.25), RES GPIO 12, DC shared with the e-paper's GPIO 6 | [user]; pins [user] |
| Analog board | The §29 loop board with a second, voltage channel (FR-40.9): its supply enable on GPIO 18 (100 kΩ pull-down on the board), its 24 V from a 2 W isolated DC-DC, isolation of at least 3 kV (C-40.6); loop on A0 – A1 as §29, the probe's 0 – 5 V through a 15 kΩ / 10 kΩ divider (0.1 %) to A2, read single-ended at ±2.048 V | [user]; parts [user] |
| Serial port | An isolated logic-level port in place of §30's MAX3232: a 1 W isolated 5 V DC-DC (at least 3 kV) powering the sensor at 5 V, a one-channel digital isolator carrying the sensor's serial line to UART 1 RX on GPIO 14; the sensor's ranging pin (4) tied high on the isolated side, so it ranges whenever powered, and GPIO 13 (the board's blue LED) left unused; its supply enable on GPIO 33 (100 kΩ pull-down) | [user]; the ranging pin [user] |
| User key | A momentary key on GPIO 1 (an RTC GPIO), active low, external 10 kΩ pull-up | [user] |
| BOOT key | GPIO 0, the board's BOOT button, as §18 | [derived from the board] |
| Field entry | Each field cable (the loop pair, the probe's three wires, the serial sensor's four) through a surge protector at the enclosure wall, bonded to one earth rod (C-40.7) | [user] |
| Bench slot | Not yet assigned [user]; its RST and GPIO 0 wired to the bench (C-40.9) | [user] |

### 40.3 Feature parity

| Feature | § | On this build |
|---------|---|---------------|
| Connectivity states, WiFi portal, status page and API, HTTP OTA, UDP log, mDNS, BOOT key, factory reset | 5, 13 – 18 | Same |
| Panel and its six lines | 5.4, 6 | The e-paper or the OLED by temperature, as on the cellular build (FR-40.7) |
| Serial log | 7 | Same, plus L50, L62 and L63 |
| Watchdog, identity, deep sleep, time, hardware clock | 19 – 22, 39 | Same; the user key on GPIO 1 |
| Weather sensor, setup check, reading history, sampling setups | 23, 24, 27, 28 | Same; the BME280 on the switched bus. FR-24.9's LED is not fitted |
| Summary email, push alerts, ThingsBoard, BLE setup | 25, 26, 31, 32 | Same |
| 4-20 mA loop | 29 | Same, on the analog board's pins of §40.2 |
| 0 – 5 V level probe | — | New on this build, in §29.2 slot 2 (FR-40.9, FR-40.10) |
| RS232 input | 30 | Same settings and rules on the isolated logic-level port (FR-40.12, C-40.4) |
| Cellular uplink (Notecard) | 33 | Same, the Notecard on the always-on bus |
| WiFi off, packed messages, ESP-NOW | 34, 37, 38 | Same |
| Modem, microSD settings, fuel gauge of the cellular build | 35 | Not on this build |

### 40.4 Settings

The §29.1 field `loop` gains a third value on this build; every other field is
as its own section says.

| Field | Format | Valid | Empty or absent |
|-------|--------|-------|-----------------|
| `loop` | `off`, `in` (the 4-20 mA channel, §29) or `volt` (the 0 – 5 V channel, FR-40.9) | one of the three; `volt` only on this build | `off` |
| `loop_lo`, `loop_hi` | With `volt`: the value at 0 V and at 5 V | as §29.1 | 0 and 5 (the value is the voltage) [user] |

With `loop` `volt`, `loop_unit` empty or absent means `V` [user]. The two
channels share slot 2 of §29.2, so one is read at a time and the reading and
history record sizes do not change [user]. The ultrasonic sensor is set up
through §30's fields; the User Manual gives its values: `rs` `listen`,
`rs_baud` 9600, `rs_format` `8N1`, `rs_timeout_ms` 3000, `rs_field` 1, and,
for the water level in metres, `rs_scale` −0.001 with `rs_offset` the sensor's
height above the reference in metres.

### 40.5 Requirements

Build:

- **FR-40.1** [Must] [user] The low-power build shall come from the same
  source tree as the other builds, the board chosen at build time
  (`CONFIG_APP_BOARD_FEATHERS3`); the other builds' images and behaviour do
  not change, and its image fits a 4 MB application slot (C-10.7) with the
  same partition table, including the 2 MB `history` partition.
- **FR-40.2** [Must] [derived from FR-35.6] Every feature §40.3 lists as the
  same shall meet its own section on this build, with the same log lines;
  where §40.3 names a change, this section's requirement replaces the named
  part only.

Power in sleep:

- **FR-40.3** [Must] [user] Before deep sleep the device shall disable LDO2
  (GPIO 39 low), so the switched bus and everything on it is unpowered; it
  enables LDO2 at each boot, window and wake before the first BME280 reading,
  and waits **10 ms** [user] before addressing the bus.
- **FR-40.4** [Must] [user] The display supply shall be on only within a
  window: switched on before FR-35.25's detection and off after FR-35.15's
  final refresh and the e-paper's deep sleep command, and never on a timer
  wake.
- **FR-40.5** [Must] [derived] Before the display supply is switched off,
  every display line of §40.2 (SCK, MOSI, both CS, DC, both resets) shall be
  set to an input without pulls, so no line powers a panel through its input
  protection.
- **FR-40.6** [Must] [derived from C-29.2] The analog board's supply (GPIO 18)
  and the serial port's supply (GPIO 33) shall be off through deep sleep,
  reset and power-on, and on only while their add-on is read.
- **NFR-40.1** [Should] [user] Asleep between windows, the board's current
  from the battery shall be measured at commissioning (the board alone, then
  with the always-on bus, the switched bus, the displays and the field boards
  added in turn) and recorded here and in the User Manual; the figure becomes
  this requirement's limit once the user accepts it.

Displays:

- **FR-40.7** [Must] [user] FR-35.2, FR-35.15, FR-35.16, FR-35.17, FR-35.24
  and FR-35.25, and the `epaper` and `oled` settings, shall apply on this
  build with the pins of §40.2; FR-35.24's modem and fuel-gauge rows show
  `modem off` and the GPIO 2 battery voltage.
- **FR-40.8** [Must] [derived] After the display supply turns on, the device
  shall wait **100 ms** [user] before FR-35.25's detection, so the OLED's
  boost has started; commissioning measures the start-up time (Phase 21).

Level probe (0 – 5 V):

- **FR-40.9** [Must] [user] With `loop` `volt`, at each reading (FR-23.1,
  FR-23.3, §28 tests) the device shall, after the BME280 reading, turn the
  analog board's supply on, wait `loop_warm_ms`, take the mean of **4**
  single-shot conversions of A2 against the isolated ground at ±2.048 V and
  128 SPS, and turn the supply off. The probe voltage is the A2 voltage × 2.5
  (the divider of §40.2), and the value
  `loop_lo + volts / 5 × (loop_hi − loop_lo)`, unclamped.
- **FR-40.10** [Must] [derived from FR-29.4] A probe voltage above **5.10 V**
  [user], an ADS1115 that does not answer, or a failed transfer shall be
  a level fault: no value is kept and L63 is logged. A 0 – 5 V output has no
  live zero, so 0 V is a valid reading and an open signal wire is not
  detected (R-46). A fault stops neither the BME280 reading, the window nor
  sleep, and is not retried within the same task run.
- **FR-40.11** [Must] [derived from FR-29.5] Each level value shall be logged
  as L62 and held in §29.2 slot 2 as the loop's value is, under `loop_name`.
- **NFR-40.2** [Must] [user] The probe voltage of FR-40.9 shall be within
  **±0.25 % of span** (12.5 mV) of a reference voltage from 0 to 5 V, at room
  temperature.

Serial sensor:

- **FR-40.12** [Must] [user] On this build §30 shall read its instrument
  through the serial port of §40.2: GPIO 33 in place of GPIO 13 as the port's
  supply, UART 1 RX on GPIO 14 with its input inverted, and no TX line (the
  instrument needs no command, `rs` `listen`); the port's settle is `rs_timeout_ms` alone, the sensor needing
  about 2 s from power-on for its first reading. Every other part of §30
  applies.
- **FR-40.13** [Should] [user] A line holding the MaxBotix no-target
  code (`R5000`) shall be a value fault, logged as L38 `nonum`, so a missing
  echo is not stored as a level.

Field protection and isolation:

- **C-40.4** [Must] [derived] This build's serial port carries logic levels
  only; an instrument with true ±12 V RS232 output needs §30's MAX3232
  board, not this port.
- **C-40.5** [Must] [user] Every field input (the loop, the level probe and
  the serial sensor) shall be galvanically isolated from the board's ground,
  including its supply, as C-29.1 says of the loop.
- **C-40.6** [Must] [user] Each isolation barrier of C-40.5 (the
  DC-DC converters and the I2C and serial isolators) shall be rated for at
  least **3 kV** for 1 minute.
- **C-40.7** [Must] [user] Each field cable shall enter through a surge
  protector (a gas discharge tube to earth, a series resistance, then a TVS
  diode sized for each line: the 24 V supplies and the 5 V signals) at the
  enclosure wall, bonded to one earth rod by a short conductor, before any
  isolated board.
- **C-40.8** [Should] [user] Field cables shall be shielded twisted pair,
  the shield bonded to earth at the station end only, laid in buried conduit
  where the site allows.
- **C-40.9** [Must] [user] The bench slot holding this build shall wire the
  board's RST and GPIO 0 to bench GPIOs through open-drain drivers, so the
  bench can reset the board, start its download mode and wake it from deep
  sleep without USB, and never powers the board through those lines.
- **C-40.10** [Must] [derived] The analog board's isolated 24 V shall supply
  a shorted loop (30 mA), its 3.3 V regulator (11 mA) and the level probe at
  its measured current together, with 20 % [user] to spare.
- **C-40.11** [Must] [user] The FeatherS3 and the Notecarrier-X shall
  each keep their own cell, charged only through their own USB-C port; no
  header pin of either board is wired to a charging source, the cells are
  never joined, and the FeatherS3's USB rail has one 5 V source at a time
  (the field's Y cable or the bench's data cable, never both). The shared
  5 V source shall be rated for at least **3 A**, so the modem's 2 A start
  and the FeatherS3's charging are carried together.

### 40.6 Verification contracts

Bench rows run on the slot that holds the FeatherS3 (not yet assigned);
`(core)` rows drive the pure cores on the host.

| ID | Precondition · stimulus | Expected observation | Must NOT happen | Tier |
|----|------------------------|----------------------|-----------------|------|
| FR-40.1 | All three builds from one commit | Each builds; the FeatherS3 image under 4 MB with the same partition table; the other images' sizes and `sdkconfig` unchanged from a build without this section | One build's sources edited to build another | host (build) |
| FR-40.3, FR-40.4, FR-40.6 (core) | Power steps for a window, a timer wake and the sleep that follows each | LDO2 on before the BME280, off before sleep; the display supply on for the window only, after the lines set to inputs; GPIO 18 and 33 low in sleep | The display supply on in a timer wake; LDO2 on in sleep | host |
| FR-40.3, FR-40.4, FR-40.5, FR-40.6 | Operator: the build asleep between windows; a meter on LDO2's output, the display supply's output, and GPIO 18 and 33 | All at 0 V; each display line at 0 V | A powered rail or a line high in sleep | bench (operator) |
| NFR-40.1 | Operator: a meter in the battery lead, USB unplugged, the build asleep; the board alone, then each part added in turn | Each current recorded in this section and the User Manual | — | bench (operator) |
| FR-40.7, FR-40.8 | Operator: both displays fitted, windows above 42 °F and below 40 °F (a cold pack on the BME280) | L50 `epaper=found oled=found`; the lines on the e-paper, then on the OLED, as FR-35.2 | A missed display; the OLED dark after FR-40.8's wait | bench (operator) |
| FR-40.9, FR-40.10 (core) | Level core with A2 at 0.000, 1.000, 2.000 and 2.045 V, `loop_lo` 0 and `loop_hi` 5 (metres); an ADS1115 error | Values 0.000, 2.500, 5.000, then a fault (5.11 V); a fault with the error | A value kept with a fault | host |
| FR-40.9, FR-40.11, NFR-40.2 | `loop` `volt`; a reference voltage on the probe input at 0, 1.25, 2.5, 3.75 and 5 V | L62 with `volt=` within 12.5 mV of each; the value on line 6, the page, `ext`, the CSV and the summary | No L62; the supply on in sleep | bench (operator) |
| FR-40.10 | A reference at 5.5 V, then the ADS1115 unplugged | L63 `over volt=5.500`; L63 with `ESP_ERR_TIMEOUT`; the window and sleep as usual | A restart; a value kept | bench (operator) |
| FR-40.12 (core) | §30 listen core with the lines `R1234`, `R0300`, `R5000` and `rs_scale` −0.001, `rs_offset` 4.000 | 2.766, 3.700, then a fault for `R5000` (with FR-40.13) | `R5000` stored as a level | host |
| FR-40.12, FR-40.13 | The MB7569-100 aimed at a board 1.00 m away, `rs` `listen` as §40.4 | L37 within 0.02 m of the expected level; with the board removed beyond range, L38 `nonum` | A garbled line; the sensor powered in sleep | bench (operator) |
| C-40.5, C-40.6 | Operator: a 500 V insulation tester between the board's ground and each field terminal | Over 100 MΩ for each | Continuity between the board's ground and a field terminal | bench (operator) |
| C-40.6, C-40.7, C-40.8 | Inspection of the parts' datasheets and the installed entry | Each isolator and DC-DC rated at 3 kV or more for 1 minute; each field cable through a protector bonded to the earth rod; shields bonded at the station end only | A field cable reaching a board without a protector | inspection |
| C-40.9 | The slot's RST line pulled low for 100 ms; GPIO 0 held low through a reset; the board asleep, RST pulsed | A boot log; the ROM's download-mode banner; a boot log with a reset cause of the RST pin | The board powered from a bench line with its own supply off | bench |
| C-40.10 | The probe's current measured at 24 V; the loop terminals shorted | The isolated 24 V stays within its rating with both, at least 20 % to spare | The DC-DC above its rated current | bench (operator) |
| C-40.11 | Inspection, then operator: both cells part-charged, the Y cable from a 5 V 3 A source into both USB-C ports; a Notecard sync forced; then the bench data cable in the FeatherS3 with the Y cable's Feather leg removed | Both boards' charge LEDs on; the sync completes with no ESP32 reset (no boot line); the bench console and flashing work | A header wire to a charging source; the cells joined; two 5 V sources on the Feather's USB rail | bench (operator) |

---

## 41. Satellite Uplink (optional)

### 41.1 Purpose & peers

A station beyond cellular coverage keeps reporting over satellite: a Blues
Notecard for Skylo in place of the §33 Notecard sends the §37 packed messages
through Skylo's geostationary network when cellular is not available, and
receives a few numeric settings back the same way [user]. Satellite carries
only what a remote site needs between visits: a daily summary of the level
with the day's status, critical alerts, remote changes to the sampling setup
and the time of a remote session (§42). Every
reading is still kept in the history (§27), and the full readings go up over
cellular or WiFi whenever either returns, from the §32 upload mark, so a
satellite period leaves no gap in ThingsBoard.

Satellite data is billed by the byte: 10 KB come with the Notecard, then about
$0.75 per KB, each packet counted as at least 50 bytes and none larger than 256
bytes (C-37.1) [derived from Blues' Starnote for Skylo datasheet]. This section
holds the station to a monthly byte budget (FR-41.9). A packet takes minutes,
not seconds, to reach Notehub.

Peers: the Notecard for Skylo on the §33 bus; Notehub, which decodes the
template notes and passes them to ThingsBoard and ntfy through the routes of
§37 (the `day` and `al` decoders); ThingsBoard, whose rule chain forwards
a changed shared attribute of FR-41.10 to Notehub's API as a device environment
variable. The cellular build (§35) has no Notecard and does not take this
section. The satellite trial (`commissioning/sat-probe`) proved the path with
its own `day.qo`; this section uses §37's `day` in its place.

### 41.2 Hardware

| Item | Value | Source |
|------|-------|--------|
| Modem | Blues Notecard for Skylo (LTE Cat-1 bis with Skylo NTN) in the Notecarrier-X's M.2 socket in place of the NOTE-MBGLW; the same bus, address, power and wiring as §33.2 | [user]; the part [user] |
| Antenna | The §33.2 LTE antenna on `MAIN` serves both cellular and Skylo (no satellite antenna); mounted with a clear view of the southern sky for the geostationary satellites, outside any metal enclosure | [derived from Blues' Starnote for Skylo datasheet]; placement [user] |
| Location | Skylo needs the station's place before it transmits; the place stored by §33's GPS check (FR-33.18) is given to the Notecard as a fixed location, so no GPS fix is taken for each satellite session | [user] |

### 41.3 Settings

Added to the §13.2 form and the BLE `settings` write (§26); all optional.

| Field | Format | Valid | Empty or absent |
|-------|--------|-------|-----------------|
| `sat` | `off`, `fallback` (cellular first, satellite only when no cellular) or `only` | one of the three; not `off` needs `uplink` `cell` and a Notecard for Skylo (FR-41.2) | the stored value; `off` when none is stored [user] |
| `sat_q` | the quantity of the daily `day` message: `temp`, `rh`, `p`, `slot1` or `slot2` (§37.3 `q`) | one of the five | the stored value; `slot2` (the loop or level probe) [user] |
| `sat_budget` | bytes of satellite data a calendar month (UTC), counted as Skylo bills them | 500 – 100000 | 2500 [user] |
| `sat_crit_lo`, `sat_crit_hi` | critical limits for `sat_q`, in its unit (§37.3), below or above which an alert is sent at once | a number; `sat_crit_lo` below `sat_crit_hi` | the stored value; none (no alert sent at once) [user] |
| `sat_alerts` | critical alerts sent over satellite a day | 0 – 10 | 1 [user] |

### 41.4 Messages and schedule

Over satellite the device sends only §37 messages in the template encoding, each
a Notefile with a compact template set up by FR-41.3:

| Message | When over satellite | Bytes billed |
|---------|---------------------|--------------|
| `day` version 2 (`pday.qo`) | once a day, after local midnight, the 24 hourly averages of `sat_q` for the day before and the day's status | 64 → 64 |
| `al` version 2 (`pal.qo`) | at once, for a critical limit only, at most `sat_alerts` a day | 11 → 50 |

At the defaults this is about 31 × 64 = 1984 bytes a month, so `sat_budget`
2500 sends every daily message (about 24 KB a year) and leaves room for about
ten critical alerts. `rd` and `st` are not sent over satellite: the day's status
rides in `day`, and the other alerts of §31 are its flags, so they reach
ThingsBoard the next morning [user].

`day` version 2 (type 2, version 2) is §37.3's `day` with three fields added
after `a` [user]:

| Field | Type | Holds | Hint |
|-------|------|-------|------|
| `flg` | uint16 | the day's flags, below | 22 |
| `mb` | uint8 | the battery at the send, in 20 mV steps (0 – 5.10 V); 0 where the board does not measure it | 21 |
| `cfg` | uint16 | the CRC-16/CCITT-FALSE of the FR-41.10 settings in use, in the order of that table, each as its template type | 22 |

Each bit of `flg` is set when its condition held at any time in the 24 hours
the message covers; the decoder turns each set bit into a ThingsBoard
attribute `flag_<name>` true and each clear bit into false [proposed]:

| Bit | Name | Set when |
|-----|------|----------|
| 0 – 9 | `t_high`, `t_low`, `rh_high`, `rh_low`, `p_high`, `p_low`, `sensor`, `rs232`, `loop`, `history` | that §31.3 condition was on |
| 10 | `test_fail` | a §28 test ended `FAIL` |
| 11 | `nosync` | the clock had no network or RTC sync (FR-22.3) |
| 12 | `remote` | remote settings were applied (FR-41.11) |
| 13 | `remote_bad` | remote settings were refused as invalid (FR-41.11), or a remote session failed (FR-42.5) |
| 14 | `budget` | the month's count passed 80 % of `sat_budget` |
| 15 | `crit` | a critical alert was on (FR-41.6) |

`al` version 2 (type 3, version 2) is §37.3's `al` with two events added for
the critical limits: 12 `crit_high` and 13 `crit_low`, `val` and `lim` in
`sat_q`'s type (float16 bits for the slots); the decoder titles them
`<hostname> <quantity> critical high` / `low` / `normal` [user].

Remote settings arrive as Notehub environment variables, each with an
environment variable template (Blues requires one for every variable received
over NTN) [derived from Blues' Notecard for Skylo quickstart]:

| Variable | Type | Setting |
|----------|------|---------|
| `s_setup` | uint8 | the normal setup, its row in §28.1 counted from 0 |
| `s_sample` | uint8 | `sample_min` |
| `s_average` | uint16 | `average_min` |
| `s_lo`, `s_hi` | float32 | the low and high alert limits of slot 2 |
| `s_session`, `s_hours` | uint32, uint8 | a remote session's start and hours (§42, FR-42.2) |

### 41.5 Requirements

Settings and setup:

- **FR-41.1** [Must] [user] The portal form (§13.2) and a BLE `settings`
  write (§26) shall carry the fields of §41.3, valid per §41.3, stored, reported
  by the `settings` read (FR-26.9) and erased by a factory reset as §33's.
- **FR-41.2** [Must] [user] With `sat` not `off`, the device shall check at
  each boot that the Notecard reports an NTN radio (`card.version`); without one,
  it behaves as with `sat` `off` and logs L67 `nomodem` once a boot.
- **FR-41.3** [Must] [user] With `sat` not `off`, at each boot where they
  differ from the Notecard's, the device shall send the compact templates of
  §41.4's messages and variables, set the fixed location of §41.2, and set
  `card.transport` to `cell-ntn` (`fallback`) or `ntn` (`only`), replacing
  FR-33.14's `cell`.

Sending:

- **FR-41.4** [Must] [user] While the Notecard is on satellite, only the
  messages of §41.4 shall be sent; the §33.4 JSON notes (`tb.qo`, `tbattr.qo`,
  `mail.qo`, and `alert.qo` in favour of `al`) are not added, and the §32
  upload mark does not move, so those readings go up from the mark when
  cellular or WiFi returns.
- **FR-41.5** [Must] [user] The `day` message shall be added once a day at
  the first wake after local midnight (FR-22.2), for `sat_q`'s hourly averages
  of the 24 whole UTC hours before, as §37.3, with the status of §41.4 in
  version 2; with the clock never set (FR-22.6), none is added.
- **FR-41.6** [Must] [user] While on satellite, a §31 alert shall not be
  sent; it sets its bit of the next `day`'s `flg`. A reading of `sat_q` above
  `sat_crit_hi` or below `sat_crit_lo` starts a critical condition, ended by a
  reading back within both; its start and its end are each added as `al`
  version 2 with `sync:true`, up to `sat_alerts` a local day, further ones that
  day logged as L67 `alerts` and not sent.
- **FR-41.7** [Must] [user] No `st` shall be added for satellite; the
  `day` message's `flg`, `mb` and `cfg` carry the status, and a change of
  remote settings shows in the next `day`.
- **FR-41.8** [Must] [user] The device shall force no satellite sync
  other than for an alert; the Notecard sends the queued messages at its own
  outbound sync (`cell_every`, §33.3).

Budget:

- **FR-41.9** [Must] [user] The device shall count the bytes Skylo bills
  for each message it adds for satellite (its template size, at least 50) in a
  month counter kept in NVS, reset at the start of each UTC month. A message
  that would take the count over `sat_budget` is not added, except an `al`,
  and is logged as L67 `budget`; the count is reported on `GET /` and past
  80 % of `sat_budget` sets the `day` flag `budget`.

Remote settings:

- **FR-41.10** [Must] [user] Only the settings of §41.4's variable table
  shall be taken over satellite: ThingsBoard's rule chain forwards a changed
  shared attribute of FR-32.17 that has a variable here to Notehub as that
  variable, and the other shared attributes wait for cellular or WiFi (FR-32.16).
- **FR-41.11** [Must] [user] At each wake the device shall read the
  variables of §41.4 from the Notecard (`env.get`); a variable whose value
  differs from the one last received is applied by FR-32.18's rules, all or
  none, logged as L66, and shown by the next `day`'s flag `remote` and its
  `cfg`; an invalid set changes nothing, is logged as L67 `invalid=<variable>`
  and sets the flag `remote_bad`.

- **C-41.1** [Must] [derived from C-37.1] No message added for satellite shall
  exceed 256 bytes in the template encoding.
- **NFR-41.1** [Should] [user] At the defaults of §41.3, a month on
  satellite shall stay within `sat_budget`, critical alerts aside.

### 41.6 Verification contracts

| ID | Precondition · stimulus | Expected observation | Must NOT happen | Tier |
|----|------------------------|----------------------|-----------------|------|
| FR-41.1 (core) | Form bodies: `sat=fallback` with `uplink` `wifi`; `sat=only` with `cell`; `sat_q=x`; `sat_budget` 499, 100001; `sat_alerts` 11; `sat_crit_lo=5&sat_crit_hi=3` | `sat`; accepted; `sat_q`; `sat_budget` twice; `sat_alerts`; `sat_crit_lo` | `sat` on without `cell` | host |
| FR-41.4, FR-41.5, FR-41.7 (core) | `uplink_core` on satellite over 8 days: readings every 10 min, a `loop` failure on day 3, the battery at 3.90 V | 8 `day` of 64 bytes, `flg` bit 8 set in the one covering day 3 and clear in the others, `mb` 195; no `st`, `al`, `tb.qo`, `tbattr.qo` or `mail.qo`; the upload mark unchanged | A JSON note added on satellite | host |
| FR-41.6 (core) | `sat_crit_hi` set; three crossings of it in one day, `sat_alerts` 1; a `t_high` alert | One `al` `crit_high`, the rest L67 `alerts`; the next `day` with bits 0 and 15 set | A second `al` that day; an `al` for `t_high` | host |
| FR-41.9, NFR-41.1 (core) | A month at the defaults, then `sat_budget` 1500; a critical alert after the budget is spent | 2500: every `day`, the flag `budget` from the day the count passes 2000; 1500: `day` dropped once spent, L67 `budget`; the alert sent; the counter reset on the 1st | The count over budget from a non-alert message | host |
| FR-41.11 (core) | Variables `s_setup=1`; then the same again; then `s_sample=7` with `s_average=15`; then a local change to `std` | Applied, L66, the next `day` with flag `remote` and the new `cfg`; nothing; refused, L67 `invalid=s_sample`, flag `remote_bad`; the local change kept | A partial set applied; a re-applied unchanged value | host |
| FR-41.3, C-41.1 | Notecard for Skylo on the bench, `sat=only`; templates read back with `note.template` | Each template as §41.4, none over 256 bytes; `card.transport` `ntn` | A note without a template | target |
| FR-41.4 – FR-41.8 | Operator, outdoors with sky view, `sat=only`, a day of readings, a §31 limit and a critical limit crossed | The `day` and `al` events in Notehub; the decoded values and flags in ThingsBoard and the critical alert in ntfy | A JSON note in Notehub from the satellite period | bench (operator) |
| FR-41.10, FR-41.11 | Operator: a shared attribute `setup` changed on ThingsBoard during the satellite run | L66 at a later wake; ThingsBoard's `cfg` matching the new setup | The change applied twice | bench (operator) |

---

## 42. Remote Session (optional)

### 42.1 Purpose & options

An operator away from the site sometimes needs the station live for a while:
to check it, change its setup, take a reading now or fetch its stored history.
A remote session keeps a station on the Notecard uplink (§33) connected for a
set number of hours from a time set in ThingsBoard, and answers requests sent
from ThingsBoard through Notehub in seconds [proposed]. Between sessions the
station sleeps as before.

The station cannot serve its own web pages (§14) over cellular: carriers give
devices private addresses behind their NAT, so nothing outside can open a
connection to the station, and the Notecard carries only Notehub's traffic, not
IP to the ESP32-S3. The options weighed [proposed]:

| Option | Reach | Builds | Running cost | Weighed |
|--------|-------|--------|--------------|---------|
| A. Session through Notehub (this section) | Status, settings, a reading now, the history as CSV; a request answered in seconds | Every Notecard build (T-Display with §33, FeatherS3) | Notecard data (500 MB included), Notehub events (5,000 a month included), about 13 mAh an hour at the Notecard | **Chosen**: no new server, no inbound port, works through any carrier |
| B. The device's own pages through a reverse tunnel | All of §14, as on the LAN | Only a PPP build (§35, the SIM7670G), which holds an IP link | A relay server (a small cloud machine, about $5 a month), the modem on for the session | Needs a server to run and secure, and the §35 board is kept for another application |
| C. A public or static address on a private APN | All of §14 | Only a PPP build | A carrier fee each month; the station open to the internet | Exposes the station to every scanner; carrier-specific |
| D. ThingsBoard RPC over MQTT | Requests from ThingsBoard, as A | Only a PPP build | As B, without the relay | A second remote path beside A; the Notecard speaks no MQTT |
| E. Session over satellite | None live: a packet takes minutes and holds 256 bytes; a year of history packed (about 470 KB) is about $350 and some 1,850 packets | §41 | | Satellite only schedules a session (FR-42.2); the session itself needs cellular |
| F. Site visit | Everything, the history by WiFi (`GET /api/history`) or BLE (§26) | Every build | Travel | Stays the way to reach a station with no cellular at all |

A session on a Notecard build costs, by Blues' published figures [derived from
Blues' low-power and low-bandwidth design guides]: the Notecard Cell+WiFi
(MBGLW) in continuous mode uses about 13 mAh an hour and, idle with
`sync:true`, about 2.5 KB an hour; each request and each answer is one Notehub
event; the history goes up through a proxy route at one event credit a
megabyte. A year of readings at 10 minutes (about 52,600 lines of
`GET /api/history` CSV, about 3.5 MB) is about 1 % of the Notecard's 500 MB
and 4 event credits. The ESP32-S3 waits in light sleep between requests.

Peers: ThingsBoard, where the operator sets the session time and sends
requests (a dashboard widget whose rule chain adds each request to Notehub
through its API) and sees the answers; Notehub, which passes requests to the
Notecard and answers and the history to ThingsBoard and the history route; for
a §41 station, the satellite variables of FR-42.2.

### 42.2 Settings and messages

The session time is set as ThingsBoard shared attributes, read with those of
FR-32.17 by FR-32.16's request; they are not settings of the form or of BLE
[proposed]:

| Attribute | Format | Valid | Empty or absent |
|-----------|--------|-------|-----------------|
| `session_at` | the start, UTC, `YYYY-MM-DDThh:mmZ` | after the time received, at most 30 days ahead | no session scheduled |
| `session_h` | hours the session stays open | 1 – 12 | 2 |

Over satellite (§41) the same two travel as the variables `s_session`
(uint32, the start in seconds since the epoch, 0 for none) and `s_hours`
(uint8), added to §41.4's variable table [proposed].

The device reports the session as the client attribute `session_state`
(`scheduled <start>`, `open until <end>`, `ended <end|request|battery>`,
`failed <nocell|invalid>`) [proposed].

Requests arrive as inbound notes in `sreq.qi`, each a JSON body with an `id`
that the answer repeats; answers go out as `srsp.qo` notes with `sync:true`,
routed to ThingsBoard [proposed]:

| `req` | Fields | Answer |
|-------|--------|--------|
| `status` | — | the §32.3 client attributes, the session's end, battery mV |
| `reading` | — | a reading taken at once, as a §32.3 telemetry entry, also kept in the history (§27) |
| `settings` | — | the `settings` read of FR-26.9, without secrets |
| `set` | the keys of FR-32.17 with values | applied by FR-32.18's rules, all or none; `remote_applied` and `remote_error` as §32.3 |
| `history` | `from`, `to` (`YYYY-MM-DD`, as FR-27.10) | the CSV of FR-27.10 for those dates posted to the proxy route `hist`; the answer gives its lines, bytes and MD5 |
| `extend` | `h`, 1 – 12 | the end moved by `h` hours, the session at most 12 hours in all |
| `end` | — | the session ends |

### 42.3 Requirements

Scheduling:

- **FR-42.1** [Must] [proposed] With `uplink` `cell` and `tb_remote` `on`,
  FR-32.16's request shall also ask for `session_at` and `session_h`; a valid
  start that differs from the one last received schedules a session, logged as
  L68 `scheduled`, and is reported in `session_state`; an invalid one changes
  nothing and is logged as L69 `invalid`.
- **FR-42.2** [Must] [proposed] With `sat` not `off` (§41), the variables
  `s_session` and `s_hours` shall schedule a session by FR-42.1's rules, read
  by FR-41.11 at each wake.
- **FR-42.3** [Must] [proposed] With a session scheduled, the device's timer
  wake shall be the earlier of its next reading (§21) and the session's start,
  so the session opens within a minute of `session_at`.

Session:

- **FR-42.4** [Must] [proposed] At the start the device shall set the
  Notecard to `hub.set` `mode` `continuous`, `sync` `true`, `outbound` 1 and
  `inbound` 1, with `card.transport` `cell` whatever `sat` says, and stay out of
  deep sleep until the session ends; it logs L68 `open` and reports
  `session_state` `open until <end>`.
- **FR-42.5** [Must] [proposed] With no Notehub connection 10 minutes after
  the start, the session shall end as failed: L69 `nocell`, `session_state`
  `failed nocell`, and on a §41 station the next `day`'s flag `remote_bad`.
- **FR-42.6** [Must] [proposed] During the session the device shall take its
  readings on the setup's schedule and add them as §33 notes, which go up at
  the next one-minute sync; between readings and requests it waits in light
  sleep and asks the Notecard for `sreq.qi` notes at least every 5 seconds.
- **FR-42.7** [Must] [proposed] Each request of §42.2 shall be answered once,
  in the order received, with its `id`; a request it does not know, or invalid
  fields, gets an answer with `error` and changes nothing. A `set` follows
  FR-32.17 and FR-32.18 exactly, so a session can change nothing a shared
  attribute could not.
- **FR-42.8** [Must] [proposed] A `history` request shall stream the CSV
  through the Notecard's binary buffer in chunks of at most 64 KB, each posted
  with `web.post` (`binary`, `offset`, `total`, its MD5 as `status`) to the
  route `hist`; a chunk that fails is read again from the history and posted
  again up to 3 times, after which the answer gives the error; readings keep
  being taken while it runs.
- **FR-42.9** [Must] [proposed] The session shall end at its end time, at an
  `end` request, or when the battery falls below 3.50 V (where the board
  measures it); the device then restores FR-33.4's `hub.set` and FR-41.3's
  transport, logs L68 `ended <reason>`, reports `session_state`, and returns to
  its timer wakes.

- **C-42.1** [Must] [proposed] No secret shall leave the device in a session:
  answers carry FR-26.9's read, never passwords or tokens, and no request
  changes network credentials, server addresses, tokens, `tb`, `tb_remote` or
  `services` (FR-32.17).
- **NFR-42.1** [Should] [proposed] A request shall be answered in ThingsBoard
  within 30 seconds of being sent, and a year of history (about 3.5 MB) shall
  reach the route within 30 minutes.
- **NFR-42.2** [Should] [proposed] A two-hour session shall use at most
  40 mAh of the Notecarrier's battery.

### 42.4 Verification contracts

| ID | Precondition · stimulus | Expected observation | Must NOT happen | Tier |
|----|------------------------|----------------------|-----------------|------|
| FR-42.1, FR-42.3 (core) | Shared attributes: `session_at` 3 days ahead, `session_h` 4; the same again; one in the past; one 31 days ahead; `session_h` 13 | Scheduled, L68, the timer wake at the start; nothing; L69 `invalid` three times | A session scheduled from an invalid value; a second schedule from an unchanged value | host |
| FR-42.2 (core) | §41 variables `s_session` a start, `s_hours` 2 | Scheduled as FR-42.1 | | host |
| FR-42.4, FR-42.6, FR-42.9 (core) | A session of 2 h with readings every 10 min; then a battery of 3.45 V an hour into another | The `hub.set` of FR-42.4 at the start; 12 readings as notes; FR-33.4's `hub.set` after 2 h, L68 `ended end`; the second ended at 3.45 V, `ended battery` | Deep sleep during a session | host |
| FR-42.7, C-42.1 (core) | Requests `status`, `settings`, `set` with `sample_min` 7 and `average_min` 15, `set` with `tb_token`, `x`, in that order | Answers in order with their `id`s; no secret in `settings`; the `set` refused whole; `tb_token` refused; `x` an `error` | A partial set applied; a secret in an answer | host |
| FR-42.8 (core) | `history` over 400 days of readings with chunks of 64 KB; the second post fails twice | Chunks with `offset`, `total` and MD5 adding up to the CSV of FR-27.10; the second sent three times; the answer's MD5 that of the whole CSV | A chunk over 64 KB; a gap in the offsets | host |
| FR-42.4 – FR-42.9, NFR-42.1 | Operator, a Notecard station on cellular, `session_at` set 10 minutes ahead in ThingsBoard | The session opens; `status`, `reading` and `set` answered in ThingsBoard within 30 s; a year's `history` at the route within 30 minutes; the station asleep after the end | Requests lost or answered twice | bench (operator) |
| FR-42.5 | Operator, the Notecard's antenna removed before the start | L69 `nocell`, `session_state` `failed nocell` after 10 minutes, the station back on its timer wakes | The station left awake | bench (operator) |
| NFR-42.2 | Operator, a Mojo or meter on the Notecarrier's battery through a two-hour session | At most 40 mAh | | bench (operator) |

---

## 11. Operational Procedures

Reading path: connect the board by USB-C (§2.2) → build per §10 → flash via
USB (C-10.5) → first boot enters the portal (FR-5.11) → join `TDS3-…` and submit
credentials (§13) → the device joins the network and shows its IP (§5.4) → open
`http://<ip>/` or `http://tds3-….local/` (§14, §17) → update by posting an image URL
to `/ota` (§15; a bad download changes nothing, an image that never reaches the
network is rolled back) → to change networks, hold BOOT 5 s (§18) and provision again, or hold the user key 3 s and change the settings over BLE
(§26) → a
hang restarts the device by itself (§19). Step-by-step instructions:
[User Manual](../UserDocumentation/User-Manual.md).

## 12. Verification & Validation

### 12.0 Test Architecture

| Tier | Runs on | Covers here |
|------|---------|-------------|
| **host** | Dev machine / CI, plain C compiler | §5 transition functions, retry delay, screen content; formatting, validation, escaping and JSON cores of §7, §13, §14, §16, §18, §20; layout of §6; configuration, partition-table and ELF inspection |
| **target** | T-Display-S3 AMOLED in SLOT1, no network peer; shipped or test build | Log formats and order, LCD failure path (LCD fault build), watchdog (hang build) |
| **bench** | T-Display-S3 AMOLED + testbench WiFi AP/station, HTTP relay, UDP log, scans, operator | Provisioning, connectivity and recovery, HTTP API, OTA and rollback (no-network build), mDNS, security scans, display content, button |

Layer mapping: L2 (§5) at host as pure functions, transitions end to end at
bench; L1 pure cores at host, wire at target/bench; L0 (§8) transitively.

Test builds (never released): LCD fault build (§5.6), no-network build (§5.6),
hang build (§19.3). Operator-confirmed checks use the testbench's blocking
operator prompt; the answer and timestamp are the evidence.

### 12.1 Acceptance test

One bench run on the shipped build: erase and flash → FR-5.11 → provision via
`enter-portal` (FR-5.13, FR-13.x) → ONLINE (NFR-5.1, FR-7.8 – 7.10) → status API,
page and counter (§14, FR-5.26) → AP outage (NFR-5.2) → OTA of a version-bumped
build hosted on the bench (FR-15.8, FR-15.10, FR-5.32, FR-5.23) → no-network build (FR-5.24) → operator factory reset
(FR-5.21) → re-provision. Then the LCD fault and hang builds. Reflash the shipped
build and leave the device ONLINE at the end.

### 12.2 Traceability

Computed by `/build` from `testing/test-plan.yaml`; never maintained in this
document.

---

## Appendix A — Constants & Configuration Catalogue

Runtime configuration (the only persisted items):

| Name | Type | Default | Valid range | Persistence | Sensitivity | Interface | Validation | Change effect | Reset behaviour |
|------|------|---------|-------------|-------------|-------------|-----------|------------|---------------|-----------------|
| `wifi_ssid` | bytes | none | 1 – 32 bytes | NVS | operational | portal (§13), BLE (§26) | FR-13.4 | immediate (T3); restart (T14) | erased by factory reset |
| `wifi_password` | bytes | none | empty or 8 – 63 bytes | NVS | secret | portal (§13), BLE write only (§26) | FR-13.4 | immediate (T3); restart (T14) | erased by factory reset |
| `mail_host`, `mail_port`, `mail_user`, `mail_to`, `mail_time`, `mail_every` | text / number | none (email off) | §25.2 | NVS | operational | portal (§13, §25.2), BLE (§26) | FR-25.1 | immediate (T3); restart (T14) | erased by factory reset |
| `mail_password` | bytes | none | 1 – 64 bytes | NVS | secret | portal (§13, §25.2), BLE write only (§26) | FR-25.1 | immediate (T3); restart (T14) | erased by factory reset |
| `loop`, `loop_name`, `loop_unit`, `loop_lo`, `loop_hi`, `loop_warm_ms` | text / number | `off` | §29.1 | NVS | operational | portal (§13), BLE (§26) | FR-29.11 | immediate (T3); restart (T14) | erased by factory reset |
| `rs`, `rs_baud`, `rs_format`, `rs_cmd`, `rs_timeout_ms`, `rs_field`, `rs_scale`, `rs_offset`, `rs_name`, `rs_unit`, `rs_dec` | text / number | `off`, 9600, `8N1` | §30.1 | NVS | operational | portal (§13), BLE (§26) | FR-30.13 | immediate (T3); restart (T14) | erased by factory reset |
| `ntfy`, `ntfy_url`, `ntfy_topic`, `alert_t_lo`, `alert_t_hi`, `alert_rh_lo`, `alert_rh_hi`, `alert_p_lo`, `alert_p_hi` | text / number | `off` | §31.2 | NVS | operational | portal (§13), BLE (§26) | FR-31.1 | immediate (T3); restart (T14) | erased by factory reset |
| `ntfy_token` | text | none | 1 – 64 characters | NVS | secret | portal (§13), BLE write only (§26) | FR-31.1 | immediate (T3); restart (T14) | erased by factory reset |
| `services` | on/off | `on` | `on`, `off` | NVS | operational | portal (§13, §32.2), BLE (§26) | FR-32.1 | immediate (T3); restart (T14) | erased by factory reset |
| `tb`, `tb_url`, `tb_every` | on/off / text / minutes | `off`; none; 60 | §32.2 | NVS | operational | portal (§13, §32.2), BLE (§26) | FR-32.1 | immediate (T3); restart (T14) | erased by factory reset |
| `tb_token` | text | none | 1 – 64 letters, digits, `-`, `_` | NVS | secret | portal (§13, §32.2), BLE write only (§26) | FR-32.1 | immediate (T3); restart (T14) | erased by factory reset |
| `tb_mark` | place in the history | before the oldest reading | §27 | NVS | operational | moved by an accepted upload (FR-32.8) | none | next upload | erased by factory reset |
| `uplink`, `nh_product`, `cell_every`, `cell_mail`, `gps` | text / number | `wifi`, none, 60, `off`, `off` | §33.3 | NVS | operational | portal (§13), BLE (§26) | FR-33.1 | immediate (T3); restart (T14) | erased by factory reset (`uplink` back to `wifi`) |
| `gps_lat`, `gps_lon`, `gps_at` | number / time | none | 5 decimals; the fix time | NVS | operational | set by a fix in the setup check (FR-33.17) | none | next attributes | erased by factory reset |
| `wifi` | on/off | `on` | `on`, `off` | NVS | operational | portal (§13, §34.2), BLE (§26) | FR-34.1 | immediate (T3, T16); restart (T14) | erased by factory reset (back to `on`) |
| `espnow_role`, `espnow_gw`, `espnow_ch`, `espnow_lr` | text / number | `off`, none, empty, `off` | §38.2 | NVS | operational | portal (§13, §38.2), BLE (§26) | FR-38.1 | immediate (T3, T16); restart (T14) | erased by factory reset with the frame counter and the node table |
| `espnow_key` | hex | none | 16 bytes | NVS | secret | portal (§13), BLE write only (§26) | FR-38.1 | immediate (T3, T16); restart (T14) | erased by factory reset |
| `setup_check` | flag | not pending | pending / not | NVS | operational | set by T3, cleared by a key (FR-24.11) | none | next non-timer boot | erased by factory reset |
| `history` | readings (§27.1) | empty | 2 MB, about 65,000 readings | flash partition (C-10.10) | operational | appended by the task; read by `GET /api/history` and BLE (§27) | FR-27.3 | next reading | erased by factory reset (FR-27.7) |

Build-time constants:

| Name | Value | Source |
|------|-------|--------|
| LCD pins, clock, offset | §2.2, C-6.1 – C-6.5 | [user]/[derived] |
| Landscape area / font cells / colours | 536 × 240 / 16 × 32 (lines 1 – 5), 12 × 24 (line 6) / `0xFFFF` on `0x0000` | [user] |
| Screen lines / max characters | 6 / 33 (lines 1 – 5), 44 (line 6) | [user] |
| LCD refresh period | 1000 ± 100 ms | [user] |
| Boot to first state line | ≤ 3000 ms | [user] |
| Boot to ONLINE | ≤ 10 s | [user] |
| Connect attempt timeout | 15 s | [user] |
| Retry delays | 1, 2, 4, 8, 15 s, ± 10 % | [user] |
| Reconnect after AP return | ≤ 30 s | [user] |
| SoftAP stop after accepted credentials | ≤ 5 s | [user] |
| SoftAP SSID / channel / IP | `TDS3-<XXXXXX>` / 1 / 192.168.4.1 | [user] |
| Hostname | `tds3-<xxxxxx>` | [user] |
| HTTP port | 80 | [user] |
| `GET /` p95 | ≤ 500 ms | [user] |
| OTA max image | 0x400000 bytes | [user] |
| Restart after OTA success / factory reset / missed confirm deadline (T10, T11, T13) | ≤ 2 s | [user] |
| OTA URL max length | 256 characters | [user] |
| OTA download stall timeout | 30 s | [user] |
| Heartbeat (`alive`) period | 10 s ± 1 s | [user, testbench compatibility contract] |
| OTA confirm deadline | 60 s from reset | [user] |
| Factory-reset hold (minimum, reset on release) | 5 s | [user] |
| Watchdog timeout | 10 s | [user] |
| Watchdog restart after timeout | ≤ 5 s | [user] |
| Bench capture clock resolution (timing contracts read on it) | ± 1 s | [derived from the bench serial ring] |
| ONLINE: AP loss to WIFI_LOST | ≤ 15 s | [user] |
| `POST /ota` answer | ≤ 2 s | [user] |
| Failed (non-stalled) OTA download to K11 | ≤ 60 s | [user] |
| Timing tolerance of restart and detection deadlines | up to + 3 s late, never early | [user] |
| UDP log destination | station gateway, port 5555 | [user] |
| Log tag | `web` | [derived] |
| Awake window after a boot that is not a deep-sleep wake | ≥ 60 s | [user] |
| Wake key | GPIO 21, active low | [user] |
| Time server | `pool.ntp.org` (SNTP) | [user] |
| Time zone | POSIX TZ string at build time, `EST5EDT,M3.2.0,M11.1.0` (US Eastern) | [user] |
| Sync after entering ONLINE | ≤ 10 s | [user] |
| Sleep current added by the key wake | ≤ 5 µA | [user] |
| BME280 address / SDA / SCL | 0x76, else 0x77 (FR-23.1) / GPIO 43 / GPIO 44 | [user] |
| BME280 mode | forced, oversampling ×1 (T, P, H), filter off | [user] |
| Task sleep period | `sample_min`, 1 – 60 min, default 10 | [user] |
| Average period | `average_min`, default 60 min, dividing a day | [user] |
| Setups | `std` 10 / 60, `fine` 1 / 15, `slow` 30 / 180 min, `custom`; tests `quick` 12 × 10 s awake, `cycle` 20 × 30 s asleep with 2 min averages (§28.1) | [user] |
| Double press | two presses of 50 ms – 1 s, the second within 1 s of the first release (FR-28.4) | [user] |
| Averages kept across sleep | 96 | [user] |
| Readings kept across sleep | 144 | [user] |
| Reading duration | ≤ 50 ms | [user] |
| I2C clock | 100 kHz, internal pull-ups on | [user] |
| Setup check hold timeout | 30 min (60 s in the sleep test builds) | [user] |
| Live reading period during the hold | 5 s | [user] |
| User key debounce | 50 ms | [user] |
| Reading line decimals (T / RH / P) | 1 / 0 / 2 | [user] |
| BLE key-window hold (user key) | 3 s | [user] |
| BLE key-window idle close | 5 min | [user] |
| BLE pairing | LE Secure Connections, passkey on line 5, DisplayOnly, at most 3 bonds kept in NVS | [user] |
| BLE failed pairings per window / provisioning pause | 3 / 60 s | [user] |
| BLE `settings` write limit | 2048 bytes | [derived from `PORTAL_BODY_MAX`] |
| Restart after an accepted key-window write (T14) | ≤ 2 s | [user] |
| Heap returned 10 s after a window | within 8 KB | [user] |
| BLE service UUID | `6e0a0001-7d3b-4c2a-9f1e-54445333424c` | [derived] |
| History record / per sector | 24-byte payload + 8-byte trailer / 128 | [derived from `flash_ring`] |
| Loop supply / settle time | 24 V isolated from the battery/system rail behind a soft-start load switch, enable GPIO 3 / `loop_warm_ms`, default 300 ms | [user] |
| Loop isolated 3.3 V / current limit | LDO from the isolated 24 V, at least 40 V in / about 30 mA | [user] |
| Loop shunt / ADC | 100 Ω 0.1 % / ADS1115 at 0x49, A0 – A1, ±4.096 V, 128 SPS, mean of 4 | [user] |
| Loop bus | I2C controller 1, SDA GPIO 15, SCL GPIO 16, 100 kHz; isolator VCC1 from GPIO 3 | [user] |
| Loop fault limits | below 3.6 mA or above 21.0 mA (NAMUR NE43) | [user] |
| Loop accuracy | ±0.25 % of span | [user] |
| RS232 port | UART1, TX GPIO 11, RX GPIO 12, transceiver supply GPIO 13 / MAX3232 (PDIP-16) / DE-9 male panel mount (DTE) or 3-pin 5.08 mm terminal | [user] |
| RS232 charge-pump settle | 10 ms after GPIO 13 goes high | [proposed] |
| RS232 line / timeout / port-on limit | ≤ 128 bytes, CR, LF or CRLF / `rs_timeout_ms` 100 – 5000 ms, default 1000 / `rs_timeout_ms` + 50 ms | [proposed] |
| Alert hysteresis (T / RH / P) | 1.0 °F / 2 % / 1.00 mbar | [user] |
| Alert budgets | ONLINE within 15 s; all alerts of a run within 10 s; asleep within 50 s (60 s with a summary) | [user] |
| Alert priority / tags | 4 `warning` (start, test FAIL) / 3 `white_check_mark` (end, test pass) | [user] |
| History append limit | ≤ 500 ms | [user] |
| Full `GET /api/history` | ≤ 30 s | [user] |
| ThingsBoard API | HTTP device API, `/api/v1/<token>/telemetry` and `/attributes` (§32.3) | [user] |
| ThingsBoard readings per request | 100 | [proposed] |
| ThingsBoard request answer / last request start on a timer wake | 10 s / 40 s after the wake | [proposed] |

## Appendix B — Lifecycle Metadata

### Verification waivers

Accepted by the user on 2026-10-24 for shipment: each part below is implemented
but cannot be verified on this testbench. Where the last column names tests,
they verify the requirement's other parts; *none — fully waived* means no part
of the requirement has a bench result. A waiver is withdrawn, and the test run, as soon as
the bench gains the capability.

| Requirement | Unverified part | Blocked by | Verified in part by |
|-------------|-----------------|------------|---------------------|
| NFR-5.1 | Boot to `web: online` within 10 s after a reset with the AP up | R-14, SensorsIot/Embedded-AI-Harness#54 | none — fully waived |
| FR-5.20 | Credentials surviving a power cycle with the AP up | R-14, #54 | TC-REC-13, TC-NET-11 (a reset keeps the credentials; ONLINE once a fresh AP starts) |
| FR-17.1 | `<hostname>.local` resolution | R-8: the bench declares no `.local` resolution | none — fully waived |
| FR-13.5 | Any name resolving to the portal over the SoftAP | R-7: the relay does not resolve through the DUT | TC-PORT-04 (the answer core, host) |
| FR-16.3 | Logging and HTTP unaffected with no UDP receiver | R-16 | none — fully waived |
| FR-15.11 | The stall clause (no byte for 30 s) | R-13, SensorsIot/Embedded-AI-Harness#55 | TC-OTA-31 (bad image, cut image, oversize, 404) |
| FR-9.4 | No SoftAP while ONLINE | R-15 | TC-SEC-12, TC-NET-11, TC-REC-13 (CONNECTING and WIFI_LOST) |
| FR-5.13 | SoftAP gone ≤ 5 s after the 200 | R-15 | JRN-01.3, JRN-01.4 (stored, CONNECTING without restart, ONLINE) |

### Superseded requirements (from the `tdisplay-s3-hello` base)

| ID | Was | Superseded by |
|----|-----|---------------|
| FR-5.1 – FR-5.5 | Hello greeting, uptime line, 1 s uptime refresh | FR-5.25, FR-5.26, §5.4 |
| FR-5.6 – FR-5.9 | Hello SHOWING/DEGRADED display failure path | FR-5.27 |
| FR-5.10 | No reset on display error | FR-5.28 |
| FR-7.1 – FR-7.4 | `hello:` log lines | FR-7.7 – FR-7.13 |
| FR-9.1 | No WiFi or BLE radio | FR-9.2 (BLE), FR-9.4, FR-9.5 |
| C-10.4 | 1 MB single-app partition | C-10.6, C-10.7 |
| FR-5.22 | Restart after the OTA receiver accepts an uploaded image | FR-5.32 (restart after a successful download) |
| FR-6.5 | Two text lines stacked inside the visible area | FR-6.9 (three lines) |
| FR-6.9 | Three text lines stacked inside the visible area | FR-6.11 (four lines) |
| FR-6.6 | The 16-character uptime line fits one row | FR-6.10 (any 20-character line) |
| FR-6.10 | Any 20-character line fits one row | FR-6.13 (any 33-character line) |
| FR-6.11 | Four text lines stacked inside the visible area | FR-6.12 (five lines) |
| FR-6.12 | Five text lines stacked inside the visible area | FR-6.14 (six lines) |
| FR-6.2 | One 16 × 32 font | FR-6.15 (a 12 × 24 font on line 6) |
| FR-23.13 | `/api/readings` with exactly five keys | FR-23.18 (`ext` added) |
| FR-27.2 | A 1.5 MB history of 16-byte records | FR-27.11 (2 MB of 24-byte records) |
| C-10.9 | Partition table with a 1.5 MB history | C-10.10 (a 2 MB history) |
| FR-7.11 | Every line redrawn by D4 logs L7 | FR-7.33 (the countdown logs once a minute) |
| FR-7.33 | Only the setup-check countdown logs L7 once a minute | FR-7.34 (the BLE key-window countdown too) |
| FR-23.6 | Line 4 as `<t>F <rh>% <p>hPa`, whole humidity and pressure | FR-23.11 (`<t>F Hum <rh>% <p> hPa`) |
| FR-23.11 | Line 4 as `<t>F Hum <rh>% <p> hPa` | FR-23.12 (`<t>F Hum <rh>% <p> mbar`) |
| FR-23.8 | `/api/readings` with pressure as `p_hpa` | FR-23.13 (pressure as `p_mbar`) |
| FR-21.10 | A timer wake never starts WiFi | FR-21.13 (except to send a due summary, FR-25.6) |
| FR-24.4 | Line 5 as `Press key to continue <mm>:<ss>` | FR-24.10 (`Press blinking key to start <mm>:<ss>`) |
| FR-24.10 | Line 5 as `Press blinking key to start <mm>:<ss>` | FR-24.12 (`Press BOOT to start <mm>:<ss>`) |
| FR-24.12 | Line 5 as `Press BOOT to start <mm>:<ss>` | FR-24.13 (`Press flashing LED to start <mm>:<ss>`) |
| FR-26.14 | Line 5 empty in the provisioning window but for the passkey | FR-26.23 (`BLE setup on`, `BLE connected`) |
| FR-24.5 | Only the user key ends the setup check | FR-24.11 (the BOOT key or the user key) |
| FR-18.2 | A short BOOT press changes nothing | FR-18.5 (except that it ends the setup check) |
| FR-23.2 | Sleep 600 s after each reading | FR-23.14 (`sample_min` minutes, set in the portal) |
| FR-23.14 | Sleep `sample_min` minutes after each reading | FR-23.17 (the period of the setup in use, §28) |
| FR-25.4 | One email from `mail_user` to `mail_to` | FR-25.12 (from the sender of FR-25.13, a separate email to `mail_to2`) |
| FR-25.7 | 30 s from connecting for the one email; L26 without the recipient | FR-25.14 (one 30 s budget for both recipients; L26 names the recipient) |
| FR-18.1 | Factory-reset event after 5 s of holding (restart while still held) | FR-18.3, FR-18.4 (event at release) |
| FR-7.15 | OTA outcome lines L11/L14 in their 0.1.0 meaning | FR-7.31 |
| FR-15.5 | 409 while an upload is in progress | FR-15.12 (while a download is in progress) |
| FR-14.1, FR-14.2 | Status page and JSON including uptime (`uptime_s`, 7 keys) | FR-14.7, FR-14.8 (6 keys, no uptime) |
| FR-9.2 | The firmware never initialises the BLE controller | FR-26.18 (only inside a setup window) |
| FR-25.2 | Settings change only by a factory reset and a new provisioning | FR-25.16 (or by a BLE write in a key window) |
| FR-15.2 – FR-15.4, FR-15.6, FR-15.7 | OTA by pushed image body (411, 413, 400, 200 answers; broken upload) | FR-15.8 – FR-15.11 (OTA by URL pull, matching the testbench's OTA flow) |
| NFR-30.2 | The RS232 hardware adds at most 10 µA with the port off or asleep (a transceiver in shutdown) | C-30.2 (no current while off: the transceiver unpowered) |
| FR-35.1 | The T-SIM7080G's modem left in PSM when the network grants it, powered off otherwise | FR-35.23 (the SIM7670G powered down after each send; LTE Cat-1 has no PSM) |
| FR-36.2 | The e-paper built last, after every other phase of both builds | FR-36.3 (on the cellular build now, before the SSD1322) |

FR-6.1 – FR-6.4 and FR-6.7 keep their IDs and meaning; only the name of the state
they apply in changed (SHOWING → LCD_OK, §5.3's display region).

```yaml
document_status: draft
fsd_version: 0.62.0
repository: https://github.com/solchjg/tdisplay-s3-amoled
baseline_commit: (the commit introducing this version)
applicable_firmware_version: none yet
author: solchjg
reviewers: []
approval_status: approved by the user except the open decisions
created: 2026-10-23
last_updated: 2026-11-12
change_history:
  - 0.0.0 2026-10-23 stub, project created from the tdisplay-s3-hello base
  - "0.1.0 2026-10-23 initial definition: station + portal, status page/API, HTTP OTA with rollback, watchdog, factory reset, UDP log, mDNS"
  - 0.1.1 2026-10-23 all proposed values accepted by the user (Appendix A)
  - 0.2.0 2026-10-23 OTA by URL pull (testbench OTA flow), FR-15.2-15.4/15.6/15.7 superseded by FR-15.8-15.11, FR-5.22 and T11/T12 restated; testbench compatibility log lines K1-K12 (FR-7.18-7.29); R-6 restated
  - 0.2.1 2026-10-23 OTA URL limit (256 characters) and download stall timeout (30 s) accepted by the user
  - "0.3.0 2026-10-23 hello world and uptime removed: FR-14.1/14.2 superseded by FR-14.7/14.8 (no uptime_s); FR-5.31 (no 'Hello, world!' or 'Up:' on screen) and FR-7.30 (no hello log tag) added"
  - "0.3.1 2026-10-23 Load-defined findings: FR-5.22, FR-6.5, FR-6.6, FR-15.5 superseded by FR-5.32, FR-6.9, FR-6.10, FR-15.12 (meaning had changed); completeness lists made explicit; NFR-5.3, NFR-15.1, NFR-15.2, NFR-19.1 and the + 3 s tolerance state numbers contracts used; FR-5.24 requires L16; FR-17.2 observed via bench WiFi events; R-6 restated"
  - 0.3.2 2026-10-23 CONNECTING × disconnected listed (counts as AF, T6); Appendix A restart row names T13
  - 0.3.3 2026-10-23 FR-5.24 test build is a no-network build (reaching ONLINE confirms, by T5; user decision); FR-7.25 moved to Phase 4; FR-5.15 contract needs no line order; FR-15.11 stall prohibits late K11; FR-9.2 air MAC range stated; R-7/R-8 defer to the bench declaration
  - 0.3.4 2026-10-23 FR-15.13 (T7 aborts an OTA download; derived from §15.1) closes OS/OF outside ONLINE; L14 logged on every boot while a slot is recorded invalid, contracts check slot= instead of L14's absence; FR-15.11 stall follow-up re-installs A; FR-19.1 evidence fits target tier
  - "0.3.5 2026-10-23 factory reset on release (user decision): FR-18.1 superseded by FR-18.3/18.4, L17b and FR-7.32 added; FR-7.15 superseded by FR-7.31; R-13 and a host stall row for FR-15.11; FR-9.4 contract reachable under §5.3; FR-15.12/15.13/NFR-19.1 contract fixes"
  - "0.3.6 2026-10-23 carried Load-defined observations: §5.3 Prohibited line scoped to stored credentials; no-network build carries a distinct version; FR-15.12/FR-15.13 contracts state the download-duration need instead of a 3 MB size"
  - "0.3.7 2026-10-23 Load-defined findings: R-13 no longer claims a host stall core (stall clause blocked on bench #55); hang build logs its anchor line and FR-19.1/NFR-19.1 timing states the bench clock resolution; NFR-5.2/NFR-5.3 tolerances aligned; FR-5.21 scans with the AP stopped; FR-5.28 resets through JTAG with the AP down; Phase 2 exit names bench #54; Appendix B parses as YAML"
  - 0.3.8 2026-10-24 verification waivers accepted by the user for shipment (Appendix B); R-14 (bench #54), R-15 (one bench radio), R-16 (UDP listener) added; bench clock resolution in Appendix A; R-12/R-13 order
  - "0.3.9 2026-10-24 waiver table honest: NFR-5.1, FR-16.3, FR-17.1 fully waived (no test evidence); FR-19.1 early-restart prohibition stated with the clock resolution"
  - "0.4.0 2026-10-29 cloned for the T-Display-S3 AMOLED 1.91\": §2.2 and C-6.1 – C-6.5 restated for the RM67162 over QSPI (pins, GPIO 38 power, MADCTL 0x60), FR-6.4/FR-6.9/FR-6.10 contracts on 536 × 240, FR-6.7 lights the panel by its brightness register; bench SLOT1; C-6.4 75 MHz set by the user"
  - 0.5.0 2026-10-30 §21 Deep Sleep (FR-21.1 – FR-21.12, NFR-21.1) from the user's answers and weather-station example, approved by the user; L18, L19
  - 0.6.0 2026-10-30 §22 Time (FR-22.1 – FR-22.5, NFR-22.1) from the user's request; L20, R-19; server, zone and 10 s deadline approved by the user
  - 0.7.0 2026-10-30 §23 Weather Sensor (FR-23.1 – FR-23.10, NFR-23.1, NFR-23.2, C-23.1) from the user's answers (BME280 0x76 on GPIO 43/44, forced x1, all outputs, 600 s); FR-6.9 superseded by FR-6.11 (four screen lines); L21, L22, R-20, R-21; approved by the user
  - 0.7.1 2026-10-30 FR-22.2 time zone set to US Eastern (EST5EDT) by the user
  - 0.7.2 2026-10-30 readings shown in °F (screen, page, L21, `temp_f` in /api/readings) by the user
  - "0.8.0 2026-10-30 from the user's request, approved by the user (humidity a whole number on line 4; 5 s live reading, 50 ms debounce): §24 Setup Check (FR-24.1 – FR-24.8, L23, L24, R-22, Phase 6); five 33-character lines (FR-6.10, FR-6.11 superseded by FR-6.13, FR-6.12); `Hum` reading line (FR-23.6 superseded by FR-23.11); countdown logging (FR-7.11 superseded by FR-7.33)"
  - "0.8.1 2026-10-30 FR-22.6 from the user's request: a reading from a never-set clock shows no time (page, `null` in /api/readings, `at=none` in L21)"
  - "0.9.0 2026-10-30 pressure in mbar instead of hPa, approved by the user: FR-23.11 superseded by FR-23.12 (line 4), FR-23.8 superseded by FR-23.13 (`p_mbar` in /api/readings); page, L21, §23.1, §23.2, contracts and R-21 say mbar; the LED next to the user key flashes at 1 Hz during the setup check (FR-24.9), and line 5 says so (FR-24.4 superseded by FR-24.10)"
  - 0.10.0 2026-10-30 §25 Summary Email (FR-25.1 – FR-25.10, NFR-25.1) from the user's answers (direct SMTP to Gmail or own server on 465/587 with a public certificate, settings in the portal, one recipient, plain text, summary at a set time and interval, a failed send dropped and logged, WiFi on a timer wake only for a due summary); FR-21.10 superseded by FR-21.13; L25, L26, R-23, Phase 7; §4.3, §9.1 and Appendix A updated
  - "0.11.0 2026-10-30 the BOOT key, next to the blinking LED, ends the setup check as well as the user key, approved by the user: FR-24.5 superseded by FR-24.11, FR-24.10 superseded by FR-24.12 (`Press BOOT to start <mm>:<ss>`), FR-18.2 superseded by FR-18.5; FR-24.9 names the LED's place by the BOOT key; FR-25.11 (a send cut short by a reset is tried once more)"
  - "0.12.0 2026-10-31 from the user's request: an optional sender address other than the login (`mail_from`, FR-25.13) and an optional second recipient sent its own email (`mail_to2`); FR-25.4 superseded by FR-25.12, FR-25.7 superseded by FR-25.14 (shared 30 s budget, L26 names the recipient); from the user's request and the proposals the user approved: readings every `sample_min` minutes and averages over `average_min` set in the portal (§23.1, FR-23.2 superseded by FR-23.14, FR-23.15, FR-23.16), the summary's min/max over every reading since the previous summary and a table of the averages (FR-25.15)"
  - "0.13.0 2026-11-01 from the user's answers to the BLE setup plan (both first setup and later changes; custom GATT service with a Web Bluetooth page; passkey on the panel; no BLE with a failed panel; provisioning window and a 3 s user-key window closing after 5 min idle; portal kept): §26 BLE Setup (FR-26.1 – FR-26.18, NFR-26.1), T3 from BLE, T14, L27 – L30, R-24, R-25, Phase 8; FR-9.2 superseded by FR-26.18, FR-25.2 superseded by FR-25.16, FR-7.33 superseded by FR-7.34 (the BLE countdown logs once a minute); §1, §2, §4.3, §9.1, §11 and Appendix A updated"
  - 0.13.1 2026-11-01 all proposed BLE values accepted by the user (FR-26.7, FR-26.8, FR-26.13, FR-26.14, FR-26.17, NFR-26.1)
  - "0.14.0 2026-11-01 from the user's request (readings history downloadable over BLE and the web page): §27 Reading History (FR-27.1 – FR-27.10, NFR-27.1, NFR-27.2; a 1.5 MB partition and a date range, both chosen by the user), BLE `history` characteristic, C-10.6 superseded by C-10.9 (history partition), L31, R-26, Phase 9; Appendix A updated"
  - 0.14.1 2026-11-01 all proposed history values accepted by the user (FR-27.7, NFR-27.1, NFR-27.2)
  - "0.15.0 2026-11-01 from the user's plan, approved as drafted: §28 Sampling Setups (FR-28.1 – FR-28.13; setups std, fine, slow, custom; tests quick and cycle; double press; verdict on line 5; result email), FR-23.14 superseded by FR-23.17, FR-21.13 gains the end-of-test wake, L32, L33, Phase 10; Appendix A updated"
  - "0.16.0 2026-11-01 from the user's answers to the 4-20 mA plan (input only, 24 V on the board, isolation in the first build, old history erased once, ±0.25 % of span, 300 ms settle, 3.6 / 21.0 mA faults, a sixth line in a smaller font): §29 4-20 mA Loop Input (FR-29.1 – FR-29.14, NFR-29.1, NFR-29.2, C-29.1), §29.2 add-on values shared with the planned RS232 input, FR-6.2 superseded by FR-6.15, FR-6.12 by FR-6.14, FR-23.13 by FR-23.18, FR-27.2 by FR-27.11, C-10.9 by C-10.10, L34 – L36, Phase 11; Appendix A updated"
  - "0.17.0 2026-11-01 from the user's answers to the RS232 plan (read one instrument, instrument not known yet so generic settings, an add-on that is off leaves no trace, a USB-RS232 adapter on the testbench host): §30 RS232 Instrument Input (FR-30.1 – FR-30.13, NFR-30.1, NFR-30.2, C-30.1), slot 1 of §29.2, L37, L38, R-27, Phase 12; pins, parts and limits proposed; Appendix A updated"
  - "0.18.0 2026-11-01 from the user's request (mark required fields, check them before sending, never lose what was typed): FR-13.8, FR-13.9 (portal form), FR-26.19 (the device's field rules over BLE, `control` `form=<offset>`), FR-26.20 (the BLE page keeps typed values)"
  - "0.18.0-ntfy 2026-11-01 from the user's answers to the ntfy plan (alerts when something needs attention, not routine readings; a self-hosted server, tested at http://192.168.2.37:8080; the plan's recommendations: limits on temperature, humidity and pressure with hysteresis, alerts may start WiFi on a timer wake, a failed alert tried once more on the next run, an optional token): §31 Push Alerts (FR-31.1 – FR-31.14, NFR-31.1, NFR-31.2; alerts a selectable item, `ntfy` on or off, from the user's request), FR-21.13 gains the alert wake, L39, L40, R-28, Phase 13; §4.3, §9.1 and Appendix A updated"
  - 0.19.0 2026-11-02 §29 (0.16.0), §30 (0.17.0) and §31 (0.18.0-ntfy, numbered on a side branch beside main's 0.18.0) brought onto main; they had merged into a branch that had already merged
  - 0.19.1 2026-11-01 review of the 0.16.0 open items. §6.1 says six lines in two fonts, FR-5.29 derives from FR-6.15 (FR-6.2 is superseded), the GPIO 3 open decision records that the header brings it out, and the §29.1 bus row (proposed since 0.16.0) is listed as an open decision; change-history entries quoted so Appendix B parses as YAML again
  - "0.20.0 2026-11-02 from the user's choice of the Blues NOTE-MBGLW over the Walter module as the first cellular option: §33 Cellular Uplink (FR-33.1 – FR-33.13, NFR-33.1, NFR-33.2; email over cellular selectable by `cell_mail`, route or off, from the user's request; ThingsBoard batches, alerts and summary email as Notecard notes routed by Notehub, the clock from the Notecard), L20 `src=cell`, L43, L44, R-31 – R-33, Phase 15; §2.2, §4.4, §9.1 and Appendix A updated; carrier, bus, power, settings and limits proposed"
  - "0.21.0 2026-11-02 the user accepted every proposed §29 value (GPIO 3 supply enable, the shunt, the loop ADS1115 and its bus, the loop field lengths and settle range, a mean of 4, the 50 ms supply margin, the FR-6.14 line tops); the loop ADS1115 runs at 128 SPS (§29.1, FR-29.2 contract); the loop supply is fed from the battery/system rail (§2, §29.1, Appendix A)"
  - "0.22.0 2026-11-02 (drafted as 0.16.0 beside main) from the user's answers to the ThingsBoard plan (self-hosted server, HTTP device API rather than MQTT, a batch every 60 minutes, `http://` first and `https://` with Let's Encrypt later, alarms in ThingsBoard's rule engine as well as the firmware's own ntfy alerts): §32 ThingsBoard Telemetry (FR-32.1 – FR-32.15, NFR-32.1), FR-21.13 and FR-25.6 gain the upload wake, FR-26.9 covers `tb_token`, a `tb` on/off switch (FR-32.14) and a `services` master switch over ntfy and ThingsBoard (FR-32.15), both from the user's request, L41, L42, R-30, Phase 14; §9.1 and Appendix A updated"
  - "0.24.0 2026-11-02 from the user's choice of an own bus for the loop isolator: the loop ADS1115 moves to I2C controller 1 on GPIO 15/16, the isolator's board side is powered from GPIO 3, so the loop draws nothing while off (§29.1, C-29.2 reworded, Appendix A, §2); §33.2 no longer names 0x49"
  - "0.23.0 2026-11-02 from the user's acceptance of the 4-20 mA plan review: §29.1 gains the soft-start load switch, the isolated 3.3 V LDO (at least 40 V in), the 30 mA loop current limit and terminal TVS, and an isolator that floats while unpowered; C-29.2 (the bus free with the loop off) and C-29.3 (a shorted loop damages nothing), R-34 (the accuracy budget at 20 mA), the Phase 11 commissioning program `loop-probe`; §2 and Appendix A updated"
  - "0.22.3 2026-11-02 from the user's clarifications: FR-24.13 (user) supersedes FR-24.12, line 5 of the hold reads `Press flashing LED to start <mm>:<ss>`; FR-26.23 (proposed) supersedes FR-26.14, line 5 says `BLE setup on` in the provisioning window, where BLE starts by itself and the user key opens no window"
  - "0.22.2 2026-11-02 BLE setup from the user's bench report (pairing failed after an erase, the BLE page unlike the WiFi form): FR-26.21 the page draws the portal form (user), FR-26.19 rules gain step, option labels, extras, placeholder, inputmode, unit, section and intro, FR-26.22 a static random address replaced while no bond is held (proposed), FR-26.18 names it"
  - "0.22.1 2026-11-02 the ntfy test server is `http://192.168.2.231` (the user's correction; `192.168.2.37:8080` is ThingsBoard): §31.2 example, Phase 13 exit criteria, R-28 (the bench AP reaches it, seen at the bench check), R-33, the FR-31.8 test row"
  - "0.25.0 2026-11-02 from the user's acceptance of the RS232 revisit (through-hole and in-stock parts, nothing drawn while off, as the loop's own bus): §30.1 the transceiver is a MAX3232 in a DIP socket powered from GPIO 13 instead of a TRS3221E / MAX3221E with FORCEOFF, GPIO 11/12 float while off, a 10 ms settle and an optional RTS (proposed), a panel-mount DE-9 or 5.08 mm terminal; FR-30.1, FR-30.2 reworded; NFR-30.2 replaced by C-30.2 (no current while off); UART1 and GPIO 11 – 13 accepted (C-30.1); §2.2, Phase 12, Appendix A updated; circuit in rs232-circuit.md"
  - "0.26.0 2026-11-02 from the user's choice of separate attachable add-on boards: §2.2 gains the add-on boards (each analog add-on with its own ADS1115) and their connectors (proposed); §29.1 and §30.1 gain a Board row; §33.2 the Notecarrier joins by GND, SDA and SCL only, from the carrier's I2C fan-out (proposed); §29.1 peer wording"
  - "0.27.0 2026-11-02 from the user's antenna question (orientation of the Blues combined LTE + GPS antenna, can it be cut): §33.2 gains an Antenna row (proposed)"
  - "0.28.0 2026-11-02 from the user's request for an option to turn WiFi off with Bluetooth left as it is: §34 WiFi Off (FR-34.1 – FR-34.12), the WIFI_OFF state with T15 and T16 (§5.3) and its screen (§5.4), T10 and T14 from WIFI_OFF, exceptions for `wifi` `off` added to FR-5.11, FR-5.12, FR-13.4, FR-21.11, FR-21.12 and FR-33.6, §9.1, Phase 16; Appendix A `wifi`"
  - "0.29.0 2026-11-02 from the user's acceptance of the Notecard review (the NOTE-MBGLW v2.5 the user owns, on a Notecarrier-X v1.3, run on cellular only): §2.2 and §33.2 name the card and carrier (user), the antenna row needs one flexible LTE antenna (no onboard antenna on the Notecarrier-X) and leaves the WiFi antenna unplaced, the power row gains the 10 mAh-a-day estimate and a 2000 mAh LiPo; FR-33.14 cellular-only transport (user); FR-33.12 one hub.sync and 60 s of time polls in a window; FR-33.7 100 readings confirmed by commissioning, else 50; NFR-33.2 a Notefile template and a measured batch; FR-34.8 covers the Notecard's WiFi; §4.3 the Notecard's WiFi out of scope; Phase 15 commissioning measures batch time and size"
  - "0.30.0 2026-11-02 from the user's request that the GPS be optionally used during setup: the `gps` setting (§33.3), FR-33.15 – FR-33.19 (a fix with the Notecard's GPS during the setup check, stored, sent to the Notecard as its fixed place and to ThingsBoard as `latitude` and `longitude`, shown on line 6 during the hold), L45, R-35; §24.1, §5.4, §33.2 antenna row, §33.4, Phase 15 and Appendix A updated"
  - "0.31.0 2026-11-03 from the user's request for an option to wire the Blues cellular without the connector: §33.2 gains a Wiring row, Qwiic or three wires soldered to the Notecarrier-X's header (GND, SDA, SCL; VIO unconnected), the LiPo optionally soldered to VBAT and GND (proposed); §2.2, the Bus, Power and ATTN rows reworded"
  - "0.32.0 2026-11-03 from the user's request for a way to keep the GPS running longer for a test, and their choice of a test in the setup box: the `gps` test (§28.1, FR-33.20, user; 20 min proposed), FR-33.15 allows the search in the test, L32 and L33 name `gps`"
  - "0.32.1 2026-11-03 from the user's bench run of the gps test, which passed 1 s after its start on the location the Notecard already held: FR-33.16 counts only a GPS fix taken after the search began (derived)"
  - "0.32.2 2026-11-03 from Blues' answer to the user: the §33.2 Bus row names the Notecarrier-X's LiPo charger (TI BQ25628E) at its fixed I2C address 0x6A"
  - "0.33.0 2026-11-04 the user approved §34 WiFi Off with its proposals (FR-34.2 credentials kept, FR-34.11 nothing sent without an uplink, the §5.4 WIFI_OFF screen) and asked for the build; FR-34.11 names the uplink section's intro instead of a WiFi hint; FR-26.19 rules gain `required_if`, so the BLE page requires the network name only with WiFi on (FR-34.3)"
  - "0.34.0 2026-11-05 from the user's request that the Blues and T-SIM7080G builds match where possible: §35 rewritten as the T-SIM7080G Build (draft): hardware and pin map, feature parity table, settings (`apn`, `cell_plmn`, `cell_mail` on/off), FR-35.5 – FR-35.20 and NFR-35.1 – NFR-35.2 (proposed) beside the user's FR-35.1 – FR-35.4, L46 – L49, R-36 – R-39; awaiting the user's review"
  - "0.34.1 2026-11-05 the user approved the §35 numbers: FR-35.1 the 60 s active-time limit, FR-35.2 the 2 °F hysteresis (back to the e-paper at 42 °F), FR-35.9 registration within 60 s, NFR-35.1 asleep within 25 s after a PSM resume and 100 s after a cold attach, NFR-35.2 under 10 MB a month; the rest of §35 stays open"
  - "0.35.0 2026-11-05 from the user's request that the e-ink screen be optional in both builds and come at the end of development: §36 E-paper Display (optional, draft) with FR-36.1 and FR-36.2 (user) and the `epaper` setting (proposed, default off); FR-35.2 and FR-35.15 apply only with the e-paper fitted, the OLED otherwise; Phase 17 (T-SIM7080G build without the e-paper) and Phase 18 (e-paper, last)"
  - "0.35.1 2026-11-05 from the user's choice of OLED: §35.2 the OLED is a 3.12\" 256 x 64 SSD1322 on SPI (user) instead of the 1.3\" SH1106 on I2C, sharing the e-paper's SPI lines with its own CS on GPIO 43 (proposed); FR-35.16 42 characters a line; R-36 names GPIO 43"
  - "0.36.0 2026-11-05 from the user's plan to use Qwiic connectors where possible with a star adapter: §2.2 and §35.2 gain a Qwiic row (user): a Qwiic star adapter on each build's sensor bus for the BME280 (and the Notecarrier-X on the T-Display build); the loop, water probe and RS232 keep JST-XH; pull-ups (proposed)"
  - "0.37.0 2026-11-05 the user approved §35 and §36 with every proposal in them (pin map, displays, off-board key, settings card, modem behaviour, settings and defaults, the `epaper` setting) and the Qwiic rows of §2.2 and §35.2; §35 and §36 are no longer drafts; §35's verification contracts come before Phase 17"
  - "0.38.0 2026-11-05 from the user's go-ahead for Phase 17: §35.6 verification contracts for FR-35.1 – FR-35.19 and NFR-35.1 – NFR-35.2 (host cores, SLOT3 bench and operator rows)"
  - "0.39.0 2026-11-06 from the user's note that the T-SIM7080G's battery-side connector is not the T-Display's Qwiic: §35.2 the sensor bus moves from the PMU's GPIO 15/7 (not on a header) to P4, GPIO 43/44 (proposed), with a caution that P4's pin order is the Qwiic order reversed; the OLED's CS moves to GPIO 46; the PMU and the loop bus share I2C controller 1; one Qwiic board keeps its pull-ups; R-36 updated"
  - "0.40.0 2026-11-06 the user's choice for `cell_plmn` (§35.4): an empty field clears the network lock, an absent field keeps the stored one"
  - "0.41.0 2026-11-06 draft for the user's review, from the user's question whether one packed format should serve every link: §37 Packed Messages (all proposed) defines the rd, day, al and st messages shared by small-payload links (satellite, LoRa), in a template encoding for Notecard links and a raw one otherwise, decoded at the edge into the §32.3 and §31.3 JSON; IP links and the §33 notes keep JSON; R-40"
  - "0.42.0 2026-11-06 draft for the user's review, from the user's approved ESP-NOW plan: §38 ESP-NOW Link (optional, both builds; node and gateway roles, LR mode, frames carrying §37 messages under AES-128-CCM, a USB-powered gateway forwarding readings and alerts to ThingsBoard's gateway API and ntfy), FR-38.1 – FR-38.20; L57 – L61; the FR-21.13 and FR-34.8 exceptions; §9.1, §35.3, Phase 19, R-41, R-42; Appendix A. Numbered after the §37 and 0.41.0 of the packed-messages draft it depends on"
  - "0.43.0 2026-11-06 the user's choice for the modem's long waits: FR-35.21, the application task is off the watch (FR-19.1) for each modem call that can pass 10 s, bounded by FR-35.9's limits"
  - "0.44.0 2026-11-06 from the user's request for timekeeping with no wireless connection (a mine shaft) and their choices on the RTC assessment: §39 Hardware Clock (optional): an RV-8803 on both builds' sensor bus (user), a valid RTC time counts as synced (user), the phone's clock over BLE (user), the RTC skips the waits of FR-33.12 and FR-35.13 (user); FR-39.6 – FR-39.9 and NFR-39.1 (proposed); L55, L56, L20 sources `rtc` and `phone`; Phase 20"
  - "0.45.0 2026-11-06 the user's choice of a screen until the SSD1322 arrives: FR-35.22, a bench build drawing the six lines on the 1.3\" OLED soldered to GPIO 13/21 (user), driven in software with RS232 and the off-board key off and the BOOT key as the user key (proposed)"
  - "0.46.0 2026-11-06 the user's choice for `/settings.txt` (FR-35.4): each value as typed, not percent-encoded, the device encodes it; empty lines and `#` lines skipped"
  - "0.47.0 2026-11-06 the user's switch of the cellular build to the Waveshare ESP32-S3-SIM7670G-4G (LTE Cat-1, SLOT2), with every recommendation of the change review approved: §35 renamed Cellular Build; §35.2 the new board, pin map, modem on UART 1, the MAX17048 on the sensor bus, the e-paper's wiring deferred to Phase 18; FR-35.1 (PSM) superseded by FR-35.23 (the modem powered down after each send); FR-35.10 `battery_pct` in place of the PSM grant; FR-35.14 the SIM7670G's GNSS; FR-35.20 card detect; FR-35.22 the 1.3\" OLED on the sensor bus; NFR-35.1 60 s, and 75 s after a failed attach (proposed); NFR-35.3 sleep current measured at commissioning (proposed); L46 without the grant; R-36 – R-38; Phase 17 on SLOT2"
  - "0.48.0 2026-11-06 the user's choice to cut the modem's supply between sends: §35.2 the board's 4G switch off and GPIO 33 switching the modem's supply (Waveshare's FAQ), held low through deep sleep; FR-35.23 `AT+CPOF` then the supply cut; FR-35.7 the supply switched on only for a send, the time step or a GPS search; R-37, NFR-35.3 and their contracts follow"
  - "0.49.0 2026-11-07 the board's V2 schematic read on SLOT2 after the first probe runs: §35.2 rewritten from it: the modem's supply on GPIO 21 (not GPIO 33, which the ESP32-S3R8's octal PSRAM takes), PWRKEY held low by the board so the modem starts with its supply, UART RX GPIO 17 / TX GPIO 18, DTR GPIO 45 and RI GPIO 40, the cell required for the modem, no card-detect line, no pull-ups on the sensor bus with the CAM switch off, the USB switch on; the loop moves to GPIO 39/41/42 and RS232 to GPIO 11/46/2 (proposed), GPIO 1 and 3 kept for the e-paper (proposed); FR-35.20 an empty slot by a mount timeout; FR-35.23 GPIO 21; R-36, R-37"
  - "0.49.1 2026-11-07 the user accepted the proposed §35.2 pins: the loop on GPIO 39/41/42, RS232 on GPIO 11/46/2, GPIO 1 and 3 kept for the e-paper's CS and BUSY"
  - "0.49.2 2026-11-07 the first SLOT2 runs with the modem working (4G switch off, GPIO 21 high: registered on LTE, AT+CPOF silent in 3.5 s): R-37 the modem did not start with the 4G switch on; §35.2 the modem's start-up drops the USB console for about a second"
  - "0.50.0 2026-11-07 the user's choice after the OLED stand-in's page writes failed on SLOT2 with only the internal pull-ups (and worked with the CAM switch on): §35.2 Sensor bus row: two external 4.7 kΩ pull-ups to 3.3 V at the header, the CAM switch staying off; the Qwiic row counts them"
  - "0.51.0 2026-11-07 the user's choice to fit Waveshare's 4.2\" e-Paper Module (V2, Rev 2.2) on the cellular build now, before the SSD1322 arrives, and to keep development messages on it: FR-36.2 superseded by FR-36.3 (the e-paper built now on the cellular build); Phase 18 beside Phase 17; §35.2 the module's wiring on SPI2 (CS 3, BUSY 1, user; RST GPIO 20, proposed); the `oled` setting and FR-35.2's e-paper in every window with `oled` `off` (proposed); FR-35.24 the status block on the sleep screen (user; its rows, proposed)"
  - "0.52.0 2026-11-08 the user's agreement to drive the OLED's reset from a GPIO, given the sensors keep their pins: §35.2 OLED row RES on GPIO 19 (proposed; the user's 3.12\" OLED Ver 2.1 shows no reset RC), its R5/R8 links for 4-wire SPI; Pins row GPIO 19 used, none left free"
  - "0.53.0 2026-11-08 the user's choice to detect plugged-in displays at each window: FR-35.25 (user; method proposed): the OLED by a 10 kΩ from its CS to GND, the e-paper by its BUSY pulse after the software reset; FR-35.2 and FR-36.1 follow; §35.4 `oled` and §36.2 `epaper` mean use when found, `epaper` `on` when none is stored (proposed); §35.2 rows; L50; §35.6 rows"
  - "0.54.0 2026-11-09 the user's choice to show the e-paper when there is no reading: FR-35.2 with both displays fitted and no reading at all uses the e-paper (was the OLED)"
  - "0.55.0 2026-11-09 from the user's bench run of the NOTE-NBGLWX, whose first card.version after power-up came back garbled (`cannot interpret JSON {io}`), and their approval of retries: FR-33.10 resends a request answered with an `{io}` error, at most 2 more times, 250 ms apart (proposed)"
  - "0.55.1 2026-11-09 from the bench run of the shipped build provisioned with uplink=cell, whose first reading had no time though the Notecard had one: FR-33.12 runs the Notecard setup and the clock step right after provisioning (T3, T16), which goes on without a restart"
  - "0.55.2 2026-11-09 from the bench run where the RTC read back a frozen time with V2F clear (the cause was the driver's register map, fixed in the shared `rv8803`; the RESET check stays as a safeguard): FR-39.2 a valid time also needs the RESET bit clear, FR-39.9 a frozen time is `rtc lost`; a §39.4 core row"
  - "0.56.0 2026-11-09 the user's choice to keep an Adafruit BME280 at its default 0x77 unmodified: FR-23.1 the sensor at 0x76, else 0x77 (user)"
  - "0.56.1 2026-11-10 the user approved 0.55.2, 0.56.0 and the §39 proposals: the pull-ups and LED row, FR-39.5's 2 s, FR-39.6, FR-39.8 and NFR-39.1"
  - "0.57.0 2026-11-10 from the bench run where the device stopped joining the bench AP with no cause in its log: FR-7.35 L6 carries the station's last disconnect reason (proposed)"
  - "0.58.0 2026-11-11 from the user's note that GPS is needed for location and the request for higher-gain antenna options: §33.2 Antenna row gains the passive default of the Notecarrier-X GPS port and the active-antenna jumper (proposed)"
  - "0.58.1 2026-11-12 from the rtc-probe bench run, which measured a positive RV-8803 trim step slowing the clock (+31 steps −6.2 ppm, −32 steps +7.7 ppm; the 24 h rate −0.99 ppm, NFR-39.1 met): the §39.4 FR-39.8 core row raises the trim of a fast clock; FR-39.8 itself is unchanged"
  - "0.58.2 2026-11-12 the user approved FR-7.35 (L6 carries the station's last disconnect reason)"
  - "0.59.0 2026-11-12 approved by the user: §40.2 takes the FeatherS3's board definitions (vertical STEMMA QT SDA GPIO 16, SCL GPIO 15), ties the MaxBotix's ranging pin high with a one-channel isolator so GPIO 13 (the blue LED) is unused (FR-40.12 without a TX line), and feeds the OLED from a 5 V step-up after the display switch; §32 remote settings from ThingsBoard shared attributes (`tb_remote`, FR-32.16 – FR-32.21, L64, L65)"
  - "0.60.0 2026-11-12 draft for the user's review, from the user's question about remote settings over satellite: §41 Satellite Uplink (all proposed) sends §37's day, al and st (version 2 with cfg) over a Notecard for Skylo within a monthly byte budget and takes five numeric settings as Notehub environment variables; L66, L67, R-48, R-49, Phase 22"
  - "0.60.1 2026-11-12 the user approved §41 Satellite Uplink with a daily `day` message and `sat_budget` 2500 by default (every daily message sent, about 24 KB a year)"
  - "0.61.0 2026-11-12 the user's choice to send status in the daily satellite record: §41 sends `day` version 2 (64 bytes: the hourly averages with `flg`, 16 condition bits, `mb` battery and `cfg`) and no `st`; §31 alerts become `day` flags, only a critical limit of `sat_q` (`sat_crit_lo`, `sat_crit_hi`, `al` version 2 events 12 and 13) is sent at once, `sat_alerts` 1 by default; the flag assignment proposed"
  - "0.62.0 2026-11-13 draft for the user's review, from the user's question about turning on remote access at a set time: §42 Remote Session (all proposed) keeps a Notecard station connected for set hours from ThingsBoard's `session_at` (or §41's `s_session`), answers requests through Notehub and uploads the history CSV to a proxy route, with the options weighed; L68, L69, R-50, R-51, Phase 23; §41.1 and the `remote_bad` flag follow"
superseded_requirements: [FR-5.1, FR-5.2, FR-5.3, FR-5.4, FR-5.5, FR-5.6, FR-5.7, FR-5.8, FR-5.9, FR-5.10, FR-7.1, FR-7.2, FR-7.3, FR-7.4, FR-9.1, C-10.4, FR-15.2, FR-15.3, FR-15.4, FR-15.6, FR-15.7, FR-14.1, FR-14.2, FR-5.22, FR-6.5, FR-6.6, FR-15.5, FR-18.1, FR-7.15, FR-6.9, FR-6.10, FR-6.11, FR-7.11, FR-23.6, FR-23.11, FR-23.8, FR-24.4, FR-21.10, FR-24.5, FR-24.10, FR-18.2, FR-25.4, FR-25.7, FR-23.2, FR-9.2, FR-25.2, FR-7.33, C-10.6, FR-23.14, FR-6.2, FR-6.12, FR-23.13, FR-27.2, C-10.9, FR-24.12, FR-26.14, NFR-30.2, FR-36.2]
open_decisions:
  - "§42 Remote Session, the whole section: the Notehub session over the other options, `session_at`/`session_h` and `s_session`/`s_hours`, the requests, 1-minute syncs and 5 s polling, 64 KB history chunks to the `hist` route and that route's endpoint (a file store the user chooses), the 10-minute connect limit, the 3.50 V cut-off, NFR-42.1 and NFR-42.2"
  - "§38 ESP-NOW Link, the items marked proposed: the frame layout and nonce, `espnow_ch` and its search (3 missed wakes), 5 frames a wake, 3 tries and 50 ms, the counter kept every 256 frames, the clock from the reply (2 s), 16 nodes a gateway, MQTT on 1883/8883 with a gateway device token, a reply only after the broker's acknowledgement, no gateway on the T-Display's Notecard, the FR-21.13 and FR-34.8 exceptions"
  - "§35.2 the e-paper's RST on GPIO 20 and the OLED's RES on GPIO 19; FR-35.25 the detection method (CS resistor, BUSY pulse) and `epaper` `on` by default; §35.4 the `oled` setting (default on) and FR-35.2's e-paper in every window with `oled` `off`; FR-35.24 the status block's 10 rows, drawn only in the final full refresh, kept in RTC memory, and their content"
  - "§37 Packed Messages, the whole section: the rd, day, al and st messages, their fields, units, Notefiles and ports, the two encodings, FR-37.1 – FR-37.7, C-37.1, NFR-37.1"
  - "§39: the breakout's pull-ups and LED cut; FR-39.5 a phone time taken more than 2 s from a synced clock; FR-39.6 a Notecard time only within 3600 s of its last sync; FR-39.8 a trim only from a measurement within 0.12 ppm and after 7 days; NFR-39.1 ±3 ppm over 24 h"
  - "§33.2 Wiring: direct wiring to the header pins GND, SDA and SCL with VIO unconnected; the LiPo optionally on VBAT and GND"
  - "FR-33.16 – FR-33.19: GPS search for the whole hold at 5 s asks, `fixed` mode after a fix, `latitude`/`longitude` ThingsBoard attributes, line 6 during the hold; `gps` default off and only with `uplink` `cell`"
  - "§2.2: JST-XH add-on sockets (loop 5, water probe 6, RS232 4 pins, pin 1 GND), pull-downs on the add-ons, an I2C fan-out with a Qwiic socket for the Notecarrier, boards changed only unpowered"
  - "FR-26.23: line 5 `BLE setup on` in the provisioning window"
  - "FR-26.22: a static random BLE address, replaced whenever no bond is held (phones keep a stale entry for the old one)"
  - "§30.1: a 10 ms charge-pump settle; RTS held asserted by the second driver (optional)"
  - "§30.1: rs_cmd 1 – 32 bytes, rs_timeout_ms 100 – 5000 (default 1000), rs_field 1 – 8, rs_dec 0 – 4, a 128-byte line"
  - "NFR-30.1: port on at most rs_timeout_ms + 50 ms"
  - "§33.3: cell_mail default off (email over cellular needs the user's mail service and route first)"
  - "§33.2: I2C 0x17 on GPIO 43/44 through Qwiic, the Notecarrier-X's own LiPo of at least 2000 mAh for modem peaks, ATTN not connected"
  - "§33.2: one vertical flexible LTE antenna on MAIN, 10 mm clearance, the combined antenna never cut, GPS and WiFi antennas unplaced"
  - "§33.2: GPS passive by default; an active GPS antenna only with the VGPS_OUT–VGPS_IN jumper, and then never a passive one"
  - "FR-33.12: one hub.sync and card.time every 5 s for up to 60 s in a window; FR-33.7: 100 readings a note, else 50; NFR-33.2: tb.qo Notefile template"
  - "§33.3: uplink default wifi, nh_product 1 – 64 bytes, cell_every 10 – 1440 min (default 60)"
  - "FR-33.4, FR-33.10: 2 s Notecard answer limit; FR-33.5: Notecard never power-cycled"
  - "FR-33.10: an `{io}` answer resent at most 2 more times, 250 ms apart"
  - "NFR-33.1: asleep within 10 s on a timer wake that sends notes; NFR-33.2: under 4 MB a month"
  - "FR-32.1 tb_token 1 – 64 characters and tb_every dividing 1440"
  - "FR-32.7 100 readings per request"
  - "FR-32.11 10 s request answer"
  - "FR-32.12 no new request after 40 s on a timer wake"
related_test_baseline: none yet
```
