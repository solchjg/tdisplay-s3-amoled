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
network after its first boot is rolled back automatically.

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
| Radio | ESP32-S3 2.4 GHz WiFi (802.11 b/g/n); BLE 5 used only inside a setup window (§26, FR-26.18), sharing the radio by ESP-IDF software coexistence | [derived from the board] |
| Power and console | USB-C on the board, which is the ESP32-S3's native USB-Serial/JTAG | [derived from the board] |
| Flash layout | Two 4 MB application slots, see C-10.6 | [user] |
| 4-20 mA loop (optional) | One 2-wire transmitter on an isolated loop side: 24 V isolated supply enabled by GPIO 3, 100 Ω shunt, ADS1115 at 0x49 behind an I2C isolator on GPIO 43/44 (§29) | [user]; pin, address and parts [proposed] |
| RS232 instrument (optional) | One instrument on a DE-9 (DTE): TRS3221E / MAX3221E transceiver, UART1 TX GPIO 11, RX GPIO 12, transceiver power GPIO 13 (§30) | [user]; pins and parts [proposed] |
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
  C-10.10, L34 – L36; the shared `ads1115` and `loop420` components; a
  commissioning program for the loop hardware.
- **Exit criteria.** With a calibrator at 4, 8, 12, 16 and 20 mA, every L34
  value is within ±0.25 % of span; a fault current logs L35 and stops
  nothing; with `loop` `off` the loop leaves no trace; the value appears on line 6, the page, `ext`, the CSV and the
  summary; with `loop` `off` nothing changes; the old history is erased once.
- **Dependencies.** §23 (readings), §27 (history), §28 (tests), §25 (mail),
  §26 (BLE settings), the carrier PCB's loop island.

### 3.12 Phase 12: RS232 instrument input

- **Scope.** The optional RS232 input as add-on slot 1 of §29.2: poll or
  listen, one number from a text line, scaled, in readings, averages,
  history, page, JSON, line 6, tests and email.
- **Deliverables.** §30, L37, L38; the shared `serial_port` component; a
  loopback commissioning program for GPIO 11 – 13; a USB-RS232 adapter on
  the testbench host for the bench tier (R-27).
- **Exit criteria.** A scripted instrument on the bench adapter is read at
  every reading in `poll` and `listen`; silence, garbage and a long line log
  L38 and stop nothing; with `rs` `off` RS232 leaves no trace; the value
  appears on line 6, the page, `ext`, the CSV and the summary.
- **Dependencies.** §29.2 (add-on values), §23, §27, §28, §25, §26, the
  carrier PCB's RS232 block.

### 3.13 Phase 13: Push alerts

- **Scope.** The ntfy settings in the portal and over BLE, limit and failure
  alerts with hysteresis, the test alert, sending on a timer wake.
- **Deliverables.** §31, the FR-21.13 exception for a due alert, L39, L40,
  R-28; the shared `ntfy_tx` component in esp-idf-components.
- **Exit criteria.** With the user's test server at `http://192.168.2.37:8080`
  a reading past a limit puts one message on the topic and the reading back
  inside it one more; an unreachable server logs L40 and the device still
  sleeps within 50 s; with `ntfy` `off` nothing changes and the settings stay.
- **Dependencies.** §23 (readings), §25 (the shared timer-wake WiFi start),
  §26 (BLE settings), §28 (test result), §29 and §30 (add-on failures).

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
| R-22 | Bench tests that provision the DUT and then expect it to sleep after the window now meet the setup check hold, and SLOT1 cannot drive GPIO 21 | Certain | Medium | Tests that need sleep press the key by operator prompt, or run the sleep test builds, whose hold times out after 60 s; the shipped 30 min is a build constant, not a runtime value |
| R-28 | The testbench offers no ntfy server; the user's test server is at `192.168.2.37:8080` on the LAN, and the bench AP network may not route to it | Medium | Medium | The cores are host-tested; the unreachable-server row runs on the target tier; if the bench AP cannot reach the server, the bench rows are blocked (testbench change request, never a project workaround) and run by the operator with the device on the user's own WiFi |
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
- Touch, GPIO 14 button, battery voltage, backlight dimming, PSRAM.
- Uptime and request counter wrap after 2³² − 1.
- Every ESP32-pack test family case not listed in a chapter here (curated subset,
  [user]).

### 4.4 Dependencies

- ESP-IDF **v6.0.2**, the image the Harness uses (`espressif/idf:v6.0.2`). [user]
- Managed component `espressif/mdns` (not bundled with IDF 6). [derived from §17]
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

A connection attempt **fails** when the station reports a disconnect, or when it
has no IP address 15 s after the attempt started.

| # | Current | Event | Guard | Next | Action | Limit |
|---|---------|-------|-------|------|--------|-------|
| T1 | BOOT | credentials read | none stored | PROVISIONING | Start SoftAP, portal, captive DNS | `web: state=PROVISIONING` ≤ 3000 ms after reset |
| T2 | BOOT | credentials read | stored | CONNECTING | Start station attempt; no SoftAP | `web: state=CONNECTING` ≤ 3000 ms after reset |
| T3 | PROVISIONING | credentials submitted (portal or BLE, FR-26.6) | valid (FR-13.4, FR-25.1) | CONNECTING | Store in NVS; answer 200; stop SoftAP; start station attempt | SoftAP gone ≤ 5 s after the answer |
| T4 | PROVISIONING | credentials submitted (portal or BLE) | invalid | PROVISIONING | Answer 400; NVS unchanged | — |
| T5 | CONNECTING | IP acquired | — | ONLINE | n := 0; start status server, mDNS, UDP log; if image pending verify, confirm it | — |
| T6 | CONNECTING | attempt failed | — | WIFI_LOST | n := n + 1; arm retry timer with delay(n) | — |
| T7 | ONLINE | disconnected | — | WIFI_LOST | Abort any OTA download (FR-15.13); stop status server, mDNS, UDP log; n := 1; arm retry timer with delay(1) | `web: state=WIFI_LOST` ≤ 15 s after the AP stops (NFR-5.3) |
| T8 | WIFI_LOST | retry timer | — | CONNECTING | Start station attempt | — |
| T9 | ONLINE | IP changed | — | ONLINE | Update the shown IP; log `web: online` again | — |
| T10 | PROVISIONING, CONNECTING, ONLINE, WIFI_LOST | factory reset (§18) | — | restart → BOOT | Abort any OTA download; erase credentials; restart | restart ≤ 2 s after the event |
| T11 | ONLINE | OTA download succeeded (FR-15.10) | — | restart → BOOT | Select new slot; restart | restart ≤ 2 s after `OTA succeeded` |
| T12 | ONLINE | OTA download failed (FR-15.11) | — | ONLINE | Boot slot unchanged | — |
| T13 | BOOT, PROVISIONING, CONNECTING, WIFI_LOST | 60 s since reset | image pending verify | restart → BOOT | Log; restart without confirming (bootloader rolls back) | restart ≤ 2 s after the deadline |
| T14 | CONNECTING, ONLINE, WIFI_LOST | BLE settings accepted (key window, FR-26.7) | — | restart → BOOT | Store the settings; notify `result`; close the window; restart | restart ≤ 2 s after the notification (FR-26.8) |

**Completeness.** Events: credentials read (CR), credentials submitted (CS), IP
acquired (IP), attempt failed (AF), disconnected (DC), retry timer (RT), IP
changed (IC), factory reset (FR), OTA succeeded (OS), OTA failed (OF), 60 s since
reset (60), BLE settings accepted (BS). A BLE `settings` write in PROVISIONING is
CS, not BS.
- Any state × BS other than CONNECTING, ONLINE, WIFI_LOST: impossible — a key
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
- Any state × reset or watchdog reset (§19): restart into BOOT; only credentials
  persist.
- **Prohibited:** while credentials are stored, any transition into
  PROVISIONING (only T10's erase and restart lead back to it through T1,
  FR-5.19); any restart other than T10, T11, T13, T14 and the watchdog.

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
check (FR-24.12) and a BLE window (FR-26.14); line 6 holds the add-on values
(FR-29.8, FR-30.10), empty with every add-on off; the table gives lines 1 – 3.

| Connectivity state | Line 1 | Line 2 | Line 3 |
|--------------------|--------|--------|--------|
| BOOT | `Starting` | (empty) | (empty) |
| PROVISIONING | `Setup WiFi` | the SoftAP SSID (FR-13.1) | `192.168.4.1` |
| CONNECTING | `Connecting` | the stored SSID, per FR-5.29 | (empty) |
| ONLINE | `IP <a.b.c.d>` | `Req: <n>` | `<hostname>.local` (FR-20.1) |
| WIFI_LOST | `WiFi lost` | `Retrying` | (empty) |

`<n>` is the request count of FR-14.6 in decimal, no leading zeros.

### 5.5 Requirements

Connectivity:

- **FR-5.11** [Must] [user] When no credentials are stored at boot, the device
  shall enter PROVISIONING (T1).
- **FR-5.12** [Must] [user] When credentials are stored at boot, the device shall
  enter CONNECTING (T2) without starting the SoftAP.
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
  entirely inside the visible area: tops at 8, 48, 88, 128, 168 and 208 px
  [proposed], 8 px apart. A line of 44 characters on line 6 shall be
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
| L6 | T6, T7 | `web: retry in <ms> ms attempt=<n>` | W |
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
| L20 | Clock synced (FR-22.5) | `web: time synced <YYYY-MM-DD>T<hh:mm:ss><±hhmm>` (local time) | I |
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
| L32 | Test started (FR-28.12) | `web: test start id=<quick\|cycle>` | I |
| L33 | Test ended (FR-28.12) | `web: test end id=<quick\|cycle> readings=<n>/<m> averages=<a>/<b> result=<pass\|fail>[ by=<key\|reset>]` | I (W when failed) |
| L34 | Loop value (FR-29.5) | `web: loop ma=<mA, 3 decimals> v=<value, 3 decimals> <loop_unit>` | I |
| L35 | Loop failed (FR-29.4) | `web: loop failed: <fault\|ERR> ma=<mA, 3 decimals, or none>` | W |
| L36 | History erased for its new format (FR-29.12) | `web: history erased reason=format` | W |
| L37 | RS232 value (FR-30.7) | `web: rs232 <rs_name>=<value, rs_dec decimals>[ <rs_unit>]` | I |
| L38 | RS232 failed (FR-30.6) | `web: rs232 failed: <timeout\|long\|nonum\|ERR>[ line=<first 32 bytes>]` | W |
| L39 | Alert delivered (FR-31.12) | `web: ntfy sent <event>` (§31.3 names) | I |
| L40 | Alert not delivered (FR-31.11) | `web: ntfy failed: <wifi\|dns\|connect\|tls\|cert\|auth\|refused\|limit\|server\|timeout> <event>` | W |
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
- **FR-7.34** [Must] [derived from R-1, FR-24.12 and FR-26.14] Each line
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
| Remote exposure | BLE GATT service inside a setup window only (§26): device info readable by anyone in range, settings only after passkey pairing; HTTP on port 80 (status, OTA request) in ONLINE; outbound HTTP to whatever URL an OTA request names; open SoftAP with portal in PROVISIONING; mDNS; outbound UDP logs; outbound SMTP over TLS to the configured mail server (§25); outbound HTTP or HTTPS to the configured ntfy server (§31) |
| Confidentiality | **Not claimed.** HTTP and UDP logs are plaintext; NVS is not encrypted, so the WiFi password is readable from flash |
| Integrity / authenticity of firmware | **Not claimed.** Anyone on the LAN can upload an image; no signature check; secure boot off |
| Credentials | One WiFi password, one mail password and an optional ntfy token, stored in NVS; never output by the device (FR-9.3, FR-25.10, FR-31.13) |

Accepted risks [user]: unauthenticated OTA (anyone on the LAN can make the device
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
  likewise; the third is the wake whose reading ends the last
  test (FR-28.11), which continues as after a key wake.
- **FR-21.11** [Must] [user] A key wake
  shall be handled like a power-on boot: the 60 s window, panel, WiFi and web
  server, then the task (local maintenance and OTA).
- **FR-21.12** [Must] [user] The device shall not sleep before WiFi
  provisioning is complete: while no credentials are stored (PROVISIONING),
  the task does not run, whatever the time since boot.
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

The sensor is a genuine BME280 (chip ID `0x60`) on I2C at address **0x76**,
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
  mode, oversampling ×1 for all three quantities, IIR filter off.
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
installer to press a key to continue, so the sensors (and later e.g. a
GPS fix) can be checked in place first. A pending mark in NVS says the check is
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
- **FR-24.12** [Must] [user] During the hold, line 5 shall show
  `Press BOOT to start <mm>:<ss>`, counting down once a second from
  `30:00` to `00:00` (25 characters).
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
| FR-24.12 (core) | Countdown line for 1800, 61, 0 s left | `Press BOOT to start 30:00`; `… 01:01`; `… 00:00` | More than 25 characters | host |
| FR-24.11, FR-24.6 (core) | Hold, then a key event; hold, then 1800 s with no key | Mark cleared, window 60 s, then the task; mark kept, the task at once | Sleep inside the hold; the mark cleared by the timeout | host |
| FR-24.1, FR-24.2, FR-24.8 | Erased NVS; provision through the portal (bench AP) | L4, then L23; no `web: sleep` for 120 s | Sleep or the task during the hold | target |
| FR-24.11 | Holding; operator presses BOOT briefly; again after power-on with the mark pending, the user key | L24 `by=key` each time, then `web: sleep` 60 – 63 s later; no L15 | Sleep earlier; no L24; a factory reset | bench (operator) |
| FR-24.6 | Sleep test build (hold timeout cut to 60 s), provisioned | L24 `by=timeout` 60 – 63 s after L23, then L21 and `web: sleep`; after a key wake, L23 again | Sleep before the timeout; no hold after the key wake | target / bench (operator) |
| FR-24.9 (core) | LED state 0, 499, 500, 999, 1000 ms into the hold; outside the hold | Lit, lit, dark, dark, lit; lit | Dark outside the hold; a period other than 1 s | host |
| FR-24.9 | Holding; operator watches the LED next to the BOOT key for 10 s, then presses BOOT | About ten flashes, then steadily lit after the press | No flashing; the LED dark after the press | bench (operator) |
| FR-24.3, FR-24.12 | Holding; operator watches the panel for 15 s, finger on the sensor | Line 5 counts down once a second; line 4 changes at least once | A frozen countdown; line 4 unchanged | bench (operator) |

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
  summary due, WiFi stays off (FR-21.13).
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
| `settings` | `6e0a0003-…` | read, write | encrypted, authenticated (MITM) | Read: a page of the stored settings of Appendix A that are not secret, then `password_set=<0\|1>&mail_password_set=<0\|1>`; after `control` `form=<offset>`, a page of the form description of FR-26.19 instead. Write: appended to the staged body |
| `result` | `6e0a0004-…` | read, notify | encrypted, authenticated (MITM) | `none` after connecting; after each `apply`, `ok` or `invalid=<field>` naming the first invalid field |
| `control` | `6e0a0005-…` | write | encrypted, authenticated (MITM) | `apply` submits the staged body (FR-26.6, FR-26.7); `done` closes a key window; `read=<offset>` sets the read page; `form=<offset>` sets the read page of the form description (FR-26.19); `forget` erases every bond (FR-26.13); any other value is answered with the ATT error *Value Not Allowed* |

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
- **FR-26.9** [Must] [derived from §9.1, FR-9.3, FR-25.10] No BLE read or
  notification shall carry `password` or `mail_password`; `settings` reads
  report only whether each is stored (`password_set`, `mail_password_set`).
- **FR-26.10** [Must] [derived from `PORTAL_BODY_MAX`] A `settings` write that
  would make the staged body longer than **2048** bytes shall be refused with
  the ATT error *Invalid Attribute Value Length*, empty the staged body, and
  leave NVS and `result` unchanged.

Security:

- **FR-26.11** [Must] [user] The device shall pair only by LE Secure
  Connections with passkey entry, declaring the IO capability *DisplayOnly*:
  a fresh random 6-digit passkey is shown on line 5 (FR-26.14) and the central
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
  hold is disconnected at once. After
  **3** failed pairings in one window it shall close a key window, or pause
  advertising for **60 s** in PROVISIONING.
- **FR-26.15** [Must] [derived from FR-26.11] At most one central shall be
  connected at a time; while one is connected the device does not advertise.

Display and log:

- **FR-26.14** [Must] [user] While a passkey is awaited, line 5 shall show
  `BLE code <nnnnnn>`; otherwise, in a key window, `BLE setup <mm>:<ss>`
  counting down the time left of FR-26.3, or `BLE connected` while a central
  is connected. In the provisioning window line 5 is empty
  except for the passkey.
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
  derived MAC addresses (base MAC + 0 … + 3).
- **NFR-26.1** [Must] [user] 10 s after a window closes, `free_heap` shall
  be within **8 KB** of its value before the window opened.

The setup page:

- **FR-26.19** [Must] [user] The device shall describe the rules of every
  field a `settings` read shows, the same rules as FR-13.8, in a form
  description read through `settings` after `control` `form=<offset>`: a
  form body with one `<name>=<rules>` per field (a secret by its name), where
  `<rules>` is itself a form body of `label`, `required`, `group` and `need`
  (required once any field of the group is filled), `type`, `minlength`,
  `maxlength`, `min`, `max`, `pattern`, `options` and `hint`. The page shall
  check every field against them before it writes, mark required fields with
  `*`, and write nothing while a field fails. A stored secret left empty is
  never required (it keeps the stored one, FR-26.7).
- **FR-26.20** [Must] [user] When `result` is `invalid=<field>`, the page shall
  keep every value typed and name the field by its label, marked until it is
  changed; values typed and not saved shall also be kept when the page
  reconnects to the same device and draws the form again.

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
| FR-26.14 (core) | Line 5 for a passkey 4207, 300 s and 61 s left, connected | `BLE code 004207`, `BLE setup 05:00`, `BLE setup 01:01`, `BLE connected` | More than 33 characters | host |
| FR-26.16 (core) | Format L27 – L30 with sample values | The §7.2 strings exactly | A passkey in any line | host |
| FR-26.17 | Key window open; relay `POST /ota` | 409, no download, the window stays open | A download | bench (operator) |
| NFR-26.1 | ONLINE; `free_heap` from `/api/status`; open and close a key window | Within 8 KB 10 s after L30 | A larger loss | bench (operator) |
| FR-26.4 | Sleep test build, timer wake; bench scans through it | No advertiser | An advertiser on a timer wake | target |
| FR-26.18 | ONLINE, no window; bench BLE scan 30 s | No advertiser at base MAC + 0 … + 3 | Such an advertiser | bench |
| FR-26.19 (core) | Read the form description; parse it and the `settings` dump | One rule set per field shown, each with a label; `ssid` required ≤ 32; `password` 8 – 63; every mail field in group `mail`, the seven of FR-25.1 `need`; port options 465,587; `mail_host` and `setup` patterns accept and refuse what the device does | A field without rules; a rule looser or stricter than §13, §25.2, §28 | host |
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

During `quick` the device stays awake as in the window (panel, WiFi, web
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
  tests that ran, unless the BLE key window shows its own line 5 (FR-26.14).
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
2-pin terminal on an isolated loop side of the carrier board. It is off by
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
| Loop supply | 24 V, isolated 1 W DC-DC from 5 V, behind a load switch | [user] |
| Supply enable | GPIO 3, active high, pulled low through sleep and reset | [proposed] |
| Shunt | 100 Ω, 0.1 %, 25 ppm/°C; 0.4 V at 4 mA, 2.0 V at 20 mA | [proposed] |
| Loop ADC | ADS1115 at 0x49 on the isolated side, differential A0 – A1, ±4.096 V range, single shot | [proposed] |
| Bus | Through an I2C isolator onto the BME280's bus (GPIO 43/44, 100 kHz, C-23.1) | [proposed] |

Settings, in the portal form (§13.2) and a BLE `settings` write (§26):

| Field | Format | Valid | Empty or absent |
|-------|--------|-------|-----------------|
| `loop` | `off` or `in` | either | `off` |
| `loop_name` | label for the screen, page, CSV column and email | 1 – 12 [proposed] characters from `A-Z a-z 0-9 _ -` | `loop` |
| `loop_unit` | unit text | 0 – 6 [proposed] printable ASCII characters, no comma | `mA` |
| `loop_lo`, `loop_hi` | the value at 4 mA and at 20 mA | decimal, at most 3 decimals, −99999 – 99999, not equal | 4 and 20 (the value is the current) |
| `loop_warm_ms` | settle time after the supply turns on | whole ms, 50 – 5000 [proposed] | 300 [user] |

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
  loop supply on, wait `loop_warm_ms`, take the mean of **4** [proposed]
  single-shot conversions of A0 – A1, and turn the supply off.
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
  `loop_warm_ms` + **50 ms** [proposed] per reading, and off through sleep,
  reset and power-on.
- **C-29.1** [Must] [user] The loop side shall be galvanically isolated from
  the board's ground, including the loop ADS1115 and its supply.

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
| FR-29.2 (core) | `ads1115` core: register writes for one conversion of A0 – A1 at ±4.096 V | Config `MUX` 000, `PGA` 001, `MODE` single shot, then a read of the conversion register | Continuous mode; another channel or range | host |
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

At each reading the board takes the transceiver out of shutdown, opens
UART1, exchanges one poll or listens, closes the UART and shuts the
transceiver down again.

| Item | Value | Source |
|------|-------|--------|
| UART | UART1, no hardware flow control | [proposed] |
| TX / RX | GPIO 11 (to the transceiver's T1IN) / GPIO 12 (from R1OUT); ADC2 pins, so no ESP32 ADC input is lost | [proposed] |
| Transceiver power | GPIO 13 to FORCEOFF, active high, pulled low (100 kΩ) through sleep and reset | [proposed] |
| Transceiver | TI TRS3221E or ADI MAX3221E: one driver, one receiver, 3.3 V, ±15 kV ESD, about 1 µA in shutdown | [proposed] |
| Connector | DE-9 male, DTE: pin 2 RXD, pin 3 TXD, pin 5 GND; a 3-pin 3.5 mm terminal footprint beside it | [proposed] |

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
  low, never install UART1, and behave as without §30.
- **FR-30.2** [Must] [user] With `rs` not `off`, at each reading (FR-23.1,
  FR-23.3, §28 tests) the device shall, after the BME280 reading, set GPIO 13
  high, open UART1 at `rs_baud` and `rs_format`, take one value by FR-30.3
  or FR-30.4, then close UART1 and set GPIO 13 low.
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
  most `rs_timeout_ms` + **50 ms** [proposed] per reading.
- **NFR-30.2** [Must] [user] With `rs` `off`, and while the device sleeps,
  the RS232 hardware shall add at most **10 µA** [proposed] to the supply
  current.

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
- **C-30.1** [Must] [proposed] UART1 on TX GPIO 11, RX GPIO 12, and the
  transceiver's FORCEOFF on GPIO 13, as in §30.1; the pins are confirmed by
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
| FR-30.1, NFR-30.2 | Shipped build with `rs` `off`; sleep | No UART1 install in the log; GPIO 13 low; the sleep current unchanged within 10 µA | GPIO 13 high; UART1 installed | target |
| FR-30.2, FR-30.3, FR-30.7, NFR-30.1 | Transceiver wired; the testbench host's USB-RS232 adapter on the DE-9 runs a scripted instrument answering `R\r` with `+0023.45 C\r`; `rs=poll&rs_cmd=R%5Cr`; reset over USB | L37 `rs232 rs232=23.450`; the script saw one `R\r` per reading; GPIO 13 high at most 1050 ms | No L37; a second command in one reading | bench |
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
| `ntfy_url` | server base URL, `http://` or `https://`, host, optional `:port`, no path, query or trailing `/`; e.g. `http://192.168.2.37:8080` | 10 – 96 bytes; host of letters, digits, `.` and `-`; port 1 – 65535 | none |
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
| FR-31.8, FR-31.12, NFR-31.2 | Provisioned with `ntfy_url=http://192.168.2.37:8080` (the user's test server) and a test topic, `alert_t_hi` set 1 °F below the room; ONLINE; the test polls `GET <url>/<topic>/json?poll=1` | L39 `t_high`; the message on the server with the title, priority 4 and the `warning` tag within 5 s of L21 | No message; two messages | bench (R-28) |
| FR-31.10, NFR-31.1 | Sleep test build, the same server and limit, a timer wake | `web: wake cause=timer`, L21, L39, `web: sleep` within 50 s; panel dark | WiFi on a timer wake with nothing due; L2 or L7 lines | target / bench (R-28) |
| FR-31.11 | Same build, `ntfy_url` pointing at an address with no server | L40 `connect` with the event, then `web: sleep` within 50 s; on the next timer wake one more try and L40; on the one after, nothing | A retry in the same run; a third try | target |
| FR-31.7 | Mail off, ntfy set; double press for `quick` | One `test pass` message with the L33 line as its body; L39 `test_pass` | A message per reading; the result email expected | bench (R-28) |
| FR-31.13, FR-31.2 | Provisioned with token `tk_<random 12 hex>`; read BLE `settings`; capture serial and UDP through a window and an alert | `ntfy_token_set=1`; the marker nowhere | The marker in any read, log or message | bench (operator) |
| FR-31.8 | The user's phone subscribed to the topic in the ntfy app | The alert shows with its title; tapping it on the LAN opens the device page | A missing title or body | bench (operator) |

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
| BME280 address / SDA / SCL | 0x76 / GPIO 43 / GPIO 44 | [user] |
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
| Loop supply / settle time | 24 V isolated, enable GPIO 3 [proposed] / `loop_warm_ms`, default 300 ms | [user] |
| Loop shunt / ADC | 100 Ω 0.1 % / ADS1115 at 0x49, A0 – A1, ±4.096 V, mean of 4 | [proposed] |
| Loop fault limits | below 3.6 mA or above 21.0 mA (NAMUR NE43) | [user] |
| Loop accuracy | ±0.25 % of span | [user] |
| RS232 port | UART1, TX GPIO 11, RX GPIO 12, transceiver FORCEOFF GPIO 13 / TRS3221E or MAX3221E / DE-9 male (DTE) | [proposed] |
| RS232 line / timeout / port-on limit | ≤ 128 bytes, CR, LF or CRLF / `rs_timeout_ms` 100 – 5000 ms, default 1000 / `rs_timeout_ms` + 50 ms | [proposed] |
| Alert hysteresis (T / RH / P) | 1.0 °F / 2 % / 1.00 mbar | [user] |
| Alert budgets | ONLINE within 15 s; all alerts of a run within 10 s; asleep within 50 s (60 s with a summary) | [user] |
| Alert priority / tags | 4 `warning` (start, test FAIL) / 3 `white_check_mark` (end, test pass) | [user] |
| History append limit | ≤ 500 ms | [user] |
| Full `GET /api/history` | ≤ 30 s | [user] |

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

FR-6.1 – FR-6.4 and FR-6.7 keep their IDs and meaning; only the name of the state
they apply in changed (SHOWING → LCD_OK, §5.3's display region).

```yaml
document_status: draft
fsd_version: 0.19.1
repository: https://github.com/solchjg/tdisplay-s3-amoled
baseline_commit: (the commit introducing this version)
applicable_firmware_version: none yet
author: solchjg
reviewers: []
approval_status: approved by the user except the open decisions
created: 2026-10-23
last_updated: 2026-11-01
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
superseded_requirements: [FR-5.1, FR-5.2, FR-5.3, FR-5.4, FR-5.5, FR-5.6, FR-5.7, FR-5.8, FR-5.9, FR-5.10, FR-7.1, FR-7.2, FR-7.3, FR-7.4, FR-9.1, C-10.4, FR-15.2, FR-15.3, FR-15.4, FR-15.6, FR-15.7, FR-14.1, FR-14.2, FR-5.22, FR-6.5, FR-6.6, FR-15.5, FR-18.1, FR-7.15, FR-6.9, FR-6.10, FR-6.11, FR-7.11, FR-23.6, FR-23.11, FR-23.8, FR-24.4, FR-21.10, FR-24.5, FR-24.10, FR-18.2, FR-25.4, FR-25.7, FR-23.2, FR-9.2, FR-25.2, FR-7.33, C-10.6, FR-23.14, FR-6.2, FR-6.12, FR-23.13, FR-27.2, C-10.9]
open_decisions:
  - "§29.1: supply enable GPIO 3 (the header brings it out, pin 4)"
  - "§29.1: shunt 100 Ω 0.1 %, loop ADS1115 at 0x49, A0 – A1 at ±4.096 V"
  - "§29.1: loop ADS1115 through an I2C isolator onto the BME280's bus (GPIO 43/44, 100 kHz)"
  - "§29.1: loop_name 1 – 12 characters, loop_unit 0 – 6, loop_warm_ms 50 – 5000"
  - "FR-29.2: mean of 4 conversions"
  - "NFR-29.2: supply on at most loop_warm_ms + 50 ms"
  - "FR-6.14: line tops at 8, 48, 88, 128, 168 and 208 px"
  - "§30.1, C-30.1: UART1 on TX GPIO 11, RX GPIO 12, transceiver FORCEOFF GPIO 13 (to confirm by loopback commissioning)"
  - "§30.1: TRS3221E or MAX3221E transceiver, DE-9 male (DTE) with a 3-pin terminal footprint"
  - "§30.1: rs_cmd 1 – 32 bytes, rs_timeout_ms 100 – 5000 (default 1000), rs_field 1 – 8, rs_dec 0 – 4, a 128-byte line"
  - "NFR-30.1, NFR-30.2: port on at most rs_timeout_ms + 50 ms; at most 10 µA with the port off or asleep"
related_test_baseline: none yet
```
