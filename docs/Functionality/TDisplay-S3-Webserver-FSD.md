# T-Display-S3 AMOLED Webserver — Functional Specification Document (FSD)

This document is the **WHAT** plane: what must be true of the firmware. The plane
map is [`../00-Overview.md`](../00-Overview.md); the other planes are
[`../Method/`](../Method/00-Overview.md) (HOW) and the
[User Manual](../UserDocumentation/User-Manual.md) (OPERATE).

**Conventions.** Requirements are `FR-x.y` (functional), `NFR-x.y` (quality) and
`C-x.y` (constraint), each with a priority `[Must/Should/May]` and a provenance tag:
`[user]` stated or approved by the user; `[derived]` a logical consequence of a
`[user]` item or a board fact; `[pack:esp32]` proposed by the ESP32 domain pack and
accepted by the user. A number marked **[proposed]** is a value this document
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
returns the device to the portal. New firmware is installed by telling the
device, over HTTP, a URL to download it from; an image that does not reach the
network after its first boot is rolled back automatically.

**Goals.** A network-attached status device that can be provisioned, observed,
updated and recovered without a USB cable, and whose every behaviour can be
verified on the Harness testbench.

**Non-goals.** Authentication, TLS, BLE, MQTT, a WiFi network list in the portal,
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
actions it returns. The WiFi radio is in exactly one role at a time: SoftAP in
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
| Radio | ESP32-S3 2.4 GHz WiFi (802.11 b/g/n); BLE present but unused (FR-9.2) | [derived from the board] |
| Power and console | USB-C on the board, which is the ESP32-S3's native USB-Serial/JTAG | [derived from the board] |
| Flash layout | Two 4 MB application slots, see C-10.6 | [user] |
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
│                       │ Deep Sleep (§21)                                 │
├──────────────────────────────────────────────────────────────────────────┤
│ L0  Foundation        │ ESP-IDF (§8): esp_wifi, esp_netif/lwIP,          │
│                       │ esp_http_server, nvs_flash, app_update,          │
│                       │ bootloader rollback, esp_task_wdt, esp_lcd,      │
│                       │ GPIO, esp_timer, FreeRTOS, esp_log;              │
│                       │ managed espressif/mdns                           │
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
  FR-7.5 – FR-7.7, FR-7.12, FR-7.13, FR-7.33, FR-7.29, FR-7.30 (image check); FR-19.1,
  NFR-19.1 (variant app); FR-9.2; the LCD fault build and the hang build
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
  operator plus FR-7.33 mirror); NFR-14.1 holds; only TCP port 80 is open.
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
- **Deliverables.** §24, FR-6.12, FR-6.13, FR-7.33, FR-23.12; the sleep test
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

---

## 4. Risks, Assumptions & Dependencies

### 4.1 Risks

| ID | Risk | Likelihood | Impact | Mitigation |
|----|------|-----------|--------|------------|
| R-1 | The serial mirror proves what the firmware *intended* to draw, not what the panel shows | Certain | Medium | Display content is confirmed by an operator (FR-5.25, FR-6.1, FR-6.3, FR-6.4); FR-7.33 makes the intent automatable |
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
| R-22 | Bench tests that provision the DUT and then expect it to sleep after the window now meet the setup check hold, and SLOT1 cannot drive GPIO 21 | Certain | Medium | Tests that need sleep press the key by operator prompt, or run the sleep test builds, whose hold times out after 60 s; the shipped 30 min is a build constant, not a runtime value |

### 4.2 Assumptions

- The board is powered from USB-C; no battery is fitted (assumed; not
  architecture-critical).
- The LAN the device joins is trusted (§9.1). [user]

### 4.3 Explicitly out of scope

- Authentication on any endpoint; TLS on anything the device serves (the summary email is sent over TLS, §25); signed firmware, secure boot, flash
  encryption (§9.1). [user]
- BLE, MQTT, IPv6, NTP, static IP, more than one stored network, a network scan
  list in the portal, a portal timeout. [user]
- OTA by pushed image body, HTTPS or authenticated downloads, OTA progress on
  the LCD, OTA of the bootloader or partition table. [user]
- mDNS service records (`_http._tcp`); only the hostname A record is answered.
- Recovering the display after a driver error; the display stays failed until
  reset (FR-5.27).
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
| T3 | PROVISIONING | credentials submitted | valid (FR-13.4) | CONNECTING | Store in NVS; answer 200; stop SoftAP; start station attempt | SoftAP gone ≤ 5 s after the answer |
| T4 | PROVISIONING | credentials submitted | invalid | PROVISIONING | Answer 400; NVS unchanged | — |
| T5 | CONNECTING | IP acquired | — | ONLINE | n := 0; start status server, mDNS, UDP log; if image pending verify, confirm it | — |
| T6 | CONNECTING | attempt failed | — | WIFI_LOST | n := n + 1; arm retry timer with delay(n) | — |
| T7 | ONLINE | disconnected | — | WIFI_LOST | Abort any OTA download (FR-15.13); stop status server, mDNS, UDP log; n := 1; arm retry timer with delay(1) | `web: state=WIFI_LOST` ≤ 15 s after the AP stops (NFR-5.3) |
| T8 | WIFI_LOST | retry timer | — | CONNECTING | Start station attempt | — |
| T9 | ONLINE | IP changed | — | ONLINE | Update the shown IP; log `web: online` again | — |
| T10 | PROVISIONING, CONNECTING, ONLINE, WIFI_LOST | factory reset (§18) | — | restart → BOOT | Abort any OTA download; erase credentials; restart | restart ≤ 2 s after the event |
| T11 | ONLINE | OTA download succeeded (FR-15.10) | — | restart → BOOT | Select new slot; restart | restart ≤ 2 s after `OTA succeeded` |
| T12 | ONLINE | OTA download failed (FR-15.11) | — | ONLINE | Boot slot unchanged | — |
| T13 | BOOT, PROVISIONING, CONNECTING, WIFI_LOST | 60 s since reset | image pending verify | restart → BOOT | Log; restart without confirming (bootloader rolls back) | restart ≤ 2 s after the deadline |

**Completeness.** Events: credentials read (CR), credentials submitted (CS), IP
acquired (IP), attempt failed (AF), disconnected (DC), retry timer (RT), IP
changed (IC), factory reset (FR), OTA succeeded (OS), OTA failed (OF), 60 s since
reset (60).
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
  FR-5.19); any restart other than T10, T11, T13 and the watchdog.

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
| D4 | LCD_OK | 1 s tick | changed, write ok | LCD_OK | Redraw each changed line; log FR-7.33 for each | 1000 ± 100 ms period |
| D5 | LCD_OK | 1 s tick | changed, write failed | LCD_FAILED | Log `web: lcd write failed: <ERR>` | same tick |
| D6 | LCD_FAILED | 1 s tick | — | LCD_FAILED | No LCD call | — |

Completeness: LCD_START × tick: impossible, the tick starts on exit from
LCD_START. LCD_OK × start done and LCD_FAILED × start done: impossible — the LCD is
started exactly once, in LCD_START. Connectivity events never change the display
region; they change the screen content that D3 – D5 compare against.

### 5.4 Screen content

Five text lines, landscape, at most 33 characters each. Line 4 is the
reading in every state (FR-23.12); line 5 is empty except during the setup
check (FR-24.12); the table gives lines 1 – 3.

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
- **FR-5.29** [Must] [derived from FR-6.2's glyph range] A displayed SSID shall
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
| FR-5.25 | Each connectivity state reached on the bench; operator prompted per state | Operator confirms the §5.4 lines; the FR-7.33 mirror lines match them | Missing, stale or extra text | n/a | bench (operator) |
| FR-5.25 (core) | Screen function for every state, with sample SSID/IP/n/hostname | The §5.4 strings exactly | Any other string | n/a | host |
| FR-5.26 | ONLINE; one `GET /api/status` | FR-7.33 line `line2="Req: <n>"` with the new n | No mirror line | ≤ 2 s after the response | bench |
| FR-5.27 | LCD fault build failing at start; USB reset; 30 s capture | Exactly one `lcdfi: call` before `web: lcd init failed:` | Any `lcdfi: call` after it | 30 s window | target |
| FR-5.31 (core) | Screen function for every §5.3 state and every display-region state | No line equals `Hello, world!`; no line starts `Up:` | Either text in any state | n/a | host |
| FR-5.31 | Shipped build, JRN-01 run from erase to ONLINE with 3 requests; operator prompted at PROVISIONING and ONLINE | No FR-7.33 mirror line with either text; operator confirms neither is on the panel | `Hello, world!` or `Up:` on the panel or in a mirror line | n/a | bench (operator) |
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

Powers the built-in RM67162 AMOLED panel, sets its brightness, renders up to three text
lines in landscape, and reports whether each driver call succeeded. Direction:
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
- **FR-6.2** [Must] [user] Text shall be rendered in a fixed-width font with a
  16 × 32 pixel character cell covering ASCII 0x20 – 0x7E.
- **FR-6.3** [Must] [user] In LCD_OK, text shall be white on a black background.
- **FR-6.4** [Must] [derived from FR-5.25] On entry to LCD_OK (D1), the full
  536 × 240 visible area shall be cleared to black, leaving no lit pixels outside
  the text lines.
- **FR-6.12** [Must] [derived from §5.4, FR-23.12 and FR-24.12] Lines 1 – 5
  shall not overlap and shall lie entirely inside the visible area.
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
| FR-6.2 | Inspect the font used by the renderer | Cell 16 × 32 px; glyphs 0x20 – 0x7E | Other cell size; proportional font | host |
| FR-6.3 | LCD_OK; operator prompted | Operator confirms white on black | Inverted or tinted colours | bench (operator) |
| FR-6.4 | Power-cycle (random panel RAM); operator prompted after the first screen | No stray pixels along any edge or between lines | Noise band along an edge; remnants | bench (operator) |
| FR-6.12 | Render five lines with the shipped layout | Each line's top row > the previous line's bottom row; all glyphs inside x ∈ [0, 535], y ∈ [0, 239] | Overlap; a pixel outside | host |
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
| L26 | Summary dropped (FR-25.7) | `web: mail failed: <wifi\|dns\|connect\|tls\|cert\|auth\|refused\|timeout>` | W |
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
- **FR-7.33** [Must] [derived from R-1 and FR-24.12] Each line redrawn by D4
  shall log L7 with the exact text drawn, except the setup-check countdown on
  line 5, which logs L7 only when its minutes change (and once at 00:00).
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
| FR-7.33 | ONLINE; one request | L7 `line2="Req: <n>"` | Text differing from the drawn line | bench |
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
| GET | `/` | — | 200 `text/html`: form with fields `ssid` and `password`, `method="post"`, `action="/connect"` |
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
| Remote exposure | HTTP on port 80 (status, OTA request) in ONLINE; outbound HTTP to whatever URL an OTA request names; open SoftAP with portal in PROVISIONING; mDNS; outbound UDP logs; outbound SMTP over TLS to the configured mail server (§25) |
| Confidentiality | **Not claimed.** HTTP and UDP logs are plaintext; NVS is not encrypted, so the WiFi password is readable from flash |
| Integrity / authenticity of firmware | **Not claimed.** Anyone on the LAN can upload an image; no signature check; secure boot off |
| Credentials | One WiFi password and one mail password, stored in NVS; never output by the device (FR-9.3, FR-25.10) |

Accepted risks [user]: unauthenticated OTA (anyone on the LAN can make the device
download and run any image from any HTTP URL); open portal SoftAP (anyone in radio
range during PROVISIONING can submit credentials or read submitted ones);
plaintext HTTP and logs; unencrypted NVS. The profile justifies these protections
only: never disclosing the passwords, keeping the radio surface to what the
product uses, and not reflecting portal input as markup (FR-13.7).

### 9.2 Requirements

- **FR-9.2** [Must] [derived from §1 non-goals] The firmware shall not initialise
  the BLE controller.
- **FR-9.3** [Must] [derived from §9.1] The device shall not output the stored
  WiFi password on serial, UDP, or any HTTP response.
- **FR-9.4** [Must] [derived from §9.1] The SoftAP shall be active only in
  PROVISIONING.
- **FR-9.5** [Must] [derived from §9.1] In ONLINE, TCP port 80 shall be the only
  open TCP port on the station address.

### 9.3 Verification contracts

| ID | Precondition · stimulus | Expected observation | Must NOT happen | Tier |
|----|------------------------|----------------------|-----------------|------|
| FR-9.2 | Inspect the linked ELF | No `esp_bt_controller_init` symbol | Symbol present | host |
| FR-9.2 (air) | Device running; bench BLE scan | No advertiser at any of the ESP32-S3's derived MAC addresses: base MAC + 0 … + 3 (station, SoftAP, BT, Ethernet; chip property) | Such an advertiser | bench |
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
| C-10.6 | Parse the built partition table binary | Exactly the five rows tabled | Any other row, offset or size | host |
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
  only while it is sent.
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

### 23.3 Requirements

- **FR-23.1** [Must] [user] Each time the task runs (§21, FR-21.4) it shall take
  one reading from the BME280 at 0x76 on SDA GPIO 43 / SCL GPIO 44, in forced
  mode, oversampling ×1 for all three quantities, IIR filter off.
- **FR-23.2** [Must] [user] After its reading the task shall tell the device to
  sleep for **600 s** (FR-21.9).
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
- **FR-23.13** [Must] [user] In ONLINE, `GET /api/readings` shall answer 200 with
  the §23.2 object holding every kept reading, newest first; `[]` when none is
  kept.
- **FR-23.9** [Must] [user] The device shall keep the newest **144** readings
  (24 h at 600 s) across deep sleep in RTC memory, dropping the oldest when
  full. A power loss or reset empties them.
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
| FR-23.13 (core) | Render the JSON with 0, 1 and 144 readings, synced and unsynced | Valid JSON, exactly the five keys per element, newest first | Extra or missing key; invalid JSON | host |
| FR-23.3, FR-23.5, NFR-23.1 | Reset over USB with the sensor fitted | L21 before the window opens, values within 14 – 113 °F, 5 – 95 %, 870 – 1085 mbar | L22; no L21 | target |
| FR-23.2, FR-23.9, FR-23.5 | Sleep test build with the period cut to 10 s, three timer wakes, then a key wake | An L21 on every timer wake; `/api/readings` lists them, newest first | A timer wake without L21; WiFi or panel on a timer wake | target / bench (operator) |
| FR-23.4 | Fault build reading address 0x77 | L22 with `ESP_ERR_TIMEOUT`, then the window, ONLINE and `web: sleep` as usual | A restart; a reading kept; no sleep | target |
| FR-23.12 | ONLINE with the sensor fitted | Operator confirms line 4 matches the latest L21 | Line 4 missing, clipped or stale | bench (operator) |
| FR-23.7, FR-23.13 | ONLINE; relay `GET /` and `GET /api/readings` | Page shows the newest reading's values; JSON parses as §23.2 and its first element equals the latest L21 | 404; a value differing from L21 | bench |
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
of the readings it kept since the last summary to one recipient. It sends
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
| `mail_user` | login name, also the sender address | 1 – 64 bytes |
| `mail_password` | for Gmail an app password | 1 – 64 bytes |
| `mail_to` | recipient address | 3 – 64 bytes, one `@` with text on both sides |
| `mail_time` | first summary, local time | `hh:mm`, 00:00 – 23:59 |
| `mail_every` | interval between summaries | whole hours, 1 – 24 |

### 25.3 Requirements

- **FR-25.1** [Must] [user] The portal form (§13.2) shall carry the fields of
  §25.2. A submission with all of them empty or absent shall be valid and leave
  email off; a submission with any of them filled shall be valid only if every
  one is valid per §25.2, else answer 400 naming the first invalid field.
- **FR-25.2** [Must] [user] Valid mail settings shall be stored in NVS with the
  WiFi credentials at T3 and erased by a factory reset (T10). Settings change
  only by a factory reset and a new provisioning.
- **FR-25.3** [Must] [user] A summary is due at `mail_time` local time (FR-22.2)
  and every `mail_every` hours after it, counted from `mail_time` each day. The
  task (§21) shall send one summary on its first run at or after a due time;
  due times missed in between (the device was off, or a send failed) are
  skipped, never sent late in a batch.
- **FR-25.4** [Must] [user] The summary shall be one plain-text email from
  `mail_user` to `mail_to` with the subject `<hostname> summary
  <YYYY-MM-DD> <hh:mm>` (local time of sending) and a body giving: the device
  name (FR-20.1), the period covered (time of the oldest and newest reading
  included), the number of readings, the newest reading in the format of
  FR-23.12, and the minimum and maximum of temperature (°F), humidity (%) and
  pressure (mbar) over the readings kept (FR-23.9) since the previous summary
  was sent, or all kept readings for the first summary after a boot.
- **FR-25.5** [Must] [user] The device shall connect to `mail_host` on
  `mail_port`: on 465 with TLS from the first byte, on 587 by plain SMTP then
  STARTTLS; it shall verify the server certificate against the ESP-IDF CA
  bundle and the host name, then authenticate with `mail_user` and
  `mail_password` (AUTH LOGIN or AUTH PLAIN, whichever the server offers).
- **FR-25.6** [Must] [user] When a summary is due on a timer wake, the task
  shall start WiFi as a station with the stored credentials, send the summary,
  stop WiFi (FR-21.7) and sleep; the panel stays dark. On a timer wake with no
  summary due, WiFi stays off (FR-21.13).
- **FR-25.7** [Must] [user] A summary that cannot be sent (no WiFi within
  **15 s**, a DNS, TCP, TLS or certificate failure, a server refusal, or no
  final `250` within **30 s** of connecting) shall be dropped and logged as L26
  with the reason; it shall not be retried, and the device goes on to sleep as
  usual.
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
- **NFR-25.1** [Must] [derived from FR-25.7] A timer wake that sends a summary
  shall be back asleep within **50 s** of the wake, success or failure.

### 25.4 Verification contracts

| ID | Precondition · stimulus | Expected observation | Must NOT happen | Tier |
|----|------------------------|----------------------|-----------------|------|
| FR-25.1 (core) | Form bodies: WiFi only; all mail fields valid; each field invalid in turn; port 25; `mail_to` without `@`; `mail_every` 0 and 25 | Valid, email off; valid, settings parsed; 400 naming that field each time | A partial mail group accepted | host |
| FR-25.3, FR-25.8 (core) | Due check for `mail_time` 07:00 and `mail_every` 6 at 06:59, 07:00, 12:59, 13:05, after a send at 07:02 the same day, after two missed slots, with the clock never set | not due, due, not due, due, not due until 13:00, one due (no batch), never due | Two summaries for one slot; a summary with the clock unset | host |
| FR-25.4, FR-25.10 (core) | Body for 0, 1 and 144 readings | Subject and body as FR-25.4; min/max correct; no password in the text | The password anywhere in the message | host |
| FR-25.5 (core) | SMTP core against scripted server replies for 465 and 587, AUTH LOGIN and PLAIN, a 535 refusal, a reply split across reads | The commands of RFC 5321 in order, dot-stuffed body, success only on the final 250; the 535 reported | Data sent before 220/EHLO; success on a refusal | host |
| FR-25.6, FR-25.9, NFR-25.1 | Sleep test build with a mail slot due on the next timer wake, test server reachable | `web: wake cause=timer`, L25, `web: sleep` within 50 s; panel dark | WiFi on a timer wake with no slot due; L2 or L7 lines | target / bench (operator) |
| FR-25.11 (core) | Due slot with no send begun; the same slot after a send begun and cut short | First try; last try (handled before the send) | A third try; the slot handled before the first try | host |
| FR-25.11 | Provisioned, a slot due on the next timer wake; the testbench resets the chip about 3 s into the wake | No L25 or L26 from the cut-short wake; L25 at the end of the next boot's window | No summary at all; two summaries | bench |
| FR-25.7 | Same build, `mail_host` unreachable | L26 with the reason, then `web: sleep` within 50 s | A retry; no sleep | target |
| FR-25.4 | Operator's own Gmail account, provisioned through the portal | The summary arrives as FR-25.4 | A missing field; mbar not stated | bench (operator) |

---

# Part E — Operations & Verification

## 11. Operational Procedures

Reading path: connect the board by USB-C (§2.2) → build per §10 → flash via
USB (C-10.5) → first boot enters the portal (FR-5.11) → join `TDS3-…` and submit
credentials (§13) → the device joins the network and shows its IP (§5.4) → open
`http://<ip>/` or `http://tds3-….local/` (§14, §17) → update by posting an image URL
to `/ota` (§15; a bad download changes nothing, an image that never reaches the
network is rolled back) → to change networks, hold BOOT 5 s (§18) and provision again → a
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
| `wifi_ssid` | bytes | none | 1 – 32 bytes | NVS | operational | portal (§13) | FR-13.4 | immediate (T3) | erased by factory reset |
| `wifi_password` | bytes | none | empty or 8 – 63 bytes | NVS | secret | portal (§13) | FR-13.4 | immediate (T3) | erased by factory reset |
| `mail_host`, `mail_port`, `mail_user`, `mail_to`, `mail_time`, `mail_every` | text / number | none (email off) | §25.2 | NVS | operational | portal (§13, §25.2) | FR-25.1 | immediate (T3) | erased by factory reset |
| `mail_password` | bytes | none | 1 – 64 bytes | NVS | secret | portal (§13, §25.2) | FR-25.1 | immediate (T3) | erased by factory reset |
| `setup_check` | flag | not pending | pending / not | NVS | operational | set by T3, cleared by a key (FR-24.11) | none | next non-timer boot | erased by factory reset |

Build-time constants:

| Name | Value | Source |
|------|-------|--------|
| LCD pins, clock, offset | §2.2, C-6.1 – C-6.5 | [user]/[derived] |
| Landscape area / font cell / colours | 536 × 240 / 16 × 32 / `0xFFFF` on `0x0000` | [user] |
| Screen lines / max characters | 5 / 33 | [user] |
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
| Task sleep period | 600 s | [user] |
| Readings kept across sleep | 144 | [user] |
| Reading duration | ≤ 50 ms | [user] |
| I2C clock | 100 kHz, internal pull-ups on | [user] |
| Setup check hold timeout | 30 min (60 s in the sleep test builds) | [user] |
| Live reading period during the hold | 5 s | [user] |
| User key debounce | 50 ms | [user] |
| Reading line decimals (T / RH / P) | 1 / 0 / 2 | [user] |

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
| FR-7.11 | Every line redrawn by D4 logs L7 | FR-7.33 (the countdown logs once a minute) |
| FR-23.6 | Line 4 as `<t>F <rh>% <p>hPa`, whole humidity and pressure | FR-23.11 (`<t>F Hum <rh>% <p> hPa`) |
| FR-23.11 | Line 4 as `<t>F Hum <rh>% <p> hPa` | FR-23.12 (`<t>F Hum <rh>% <p> mbar`) |
| FR-23.8 | `/api/readings` with pressure as `p_hpa` | FR-23.13 (pressure as `p_mbar`) |
| FR-21.10 | A timer wake never starts WiFi | FR-21.13 (except to send a due summary, FR-25.6) |
| FR-24.4 | Line 5 as `Press key to continue <mm>:<ss>` | FR-24.10 (`Press blinking key to start <mm>:<ss>`) |
| FR-24.10 | Line 5 as `Press blinking key to start <mm>:<ss>` | FR-24.12 (`Press BOOT to start <mm>:<ss>`) |
| FR-24.5 | Only the user key ends the setup check | FR-24.11 (the BOOT key or the user key) |
| FR-18.2 | A short BOOT press changes nothing | FR-18.5 (except that it ends the setup check) |
| FR-18.1 | Factory-reset event after 5 s of holding (restart while still held) | FR-18.3, FR-18.4 (event at release) |
| FR-7.15 | OTA outcome lines L11/L14 in their 0.1.0 meaning | FR-7.31 |
| FR-15.5 | 409 while an upload is in progress | FR-15.12 (while a download is in progress) |
| FR-14.1, FR-14.2 | Status page and JSON including uptime (`uptime_s`, 7 keys) | FR-14.7, FR-14.8 (6 keys, no uptime) |
| FR-15.2 – FR-15.4, FR-15.6, FR-15.7 | OTA by pushed image body (411, 413, 400, 200 answers; broken upload) | FR-15.8 – FR-15.11 (OTA by URL pull, matching the testbench's OTA flow) |

FR-6.1 – FR-6.4 and FR-6.7 keep their IDs and meaning; only the name of the state
they apply in changed (SHOWING → LCD_OK, §5.3's display region).

```yaml
document_status: draft
fsd_version: 0.11.0
repository: https://github.com/solchjg/tdisplay-s3-amoled
baseline_commit: (the commit introducing this version)
applicable_firmware_version: none yet
author: solchjg
reviewers: []
approval_status: all requirements and values approved by the user
created: 2026-10-23
last_updated: 2026-10-30
change_history:
  - 0.0.0 2026-10-23 stub, project created from the tdisplay-s3-hello base
  - 0.1.0 2026-10-23 initial definition: station + portal, status page/API, HTTP OTA with rollback, watchdog, factory reset, UDP log, mDNS
  - 0.1.1 2026-10-23 all proposed values accepted by the user (Appendix A)
  - 0.2.0 2026-10-23 OTA by URL pull (testbench OTA flow), FR-15.2-15.4/15.6/15.7 superseded by FR-15.8-15.11, FR-5.22 and T11/T12 restated; testbench compatibility log lines K1-K12 (FR-7.18-7.29); R-6 restated
  - 0.2.1 2026-10-23 OTA URL limit (256 characters) and download stall timeout (30 s) accepted by the user
  - 0.3.0 2026-10-23 hello world and uptime removed: FR-14.1/14.2 superseded by FR-14.7/14.8 (no uptime_s); FR-5.31 (no 'Hello, world!' or 'Up:' on screen) and FR-7.30 (no hello log tag) added
  - 0.3.1 2026-10-23 Load-defined findings: FR-5.22, FR-6.5, FR-6.6, FR-15.5 superseded by FR-5.32, FR-6.9, FR-6.10, FR-15.12 (meaning had changed); completeness lists made explicit; NFR-5.3, NFR-15.1, NFR-15.2, NFR-19.1 and the + 3 s tolerance state numbers contracts used; FR-5.24 requires L16; FR-17.2 observed via bench WiFi events; R-6 restated
  - 0.3.2 2026-10-23 CONNECTING × disconnected listed (counts as AF, T6); Appendix A restart row names T13
  - 0.3.3 2026-10-23 FR-5.24 test build is a no-network build (reaching ONLINE confirms, by T5; user decision); FR-7.25 moved to Phase 4; FR-5.15 contract needs no line order; FR-15.11 stall prohibits late K11; FR-9.2 air MAC range stated; R-7/R-8 defer to the bench declaration
  - 0.3.4 2026-10-23 FR-15.13 (T7 aborts an OTA download; derived from §15.1) closes OS/OF outside ONLINE; L14 logged on every boot while a slot is recorded invalid, contracts check slot= instead of L14's absence; FR-15.11 stall follow-up re-installs A; FR-19.1 evidence fits target tier
  - 0.3.5 2026-10-23 factory reset on release (user decision): FR-18.1 superseded by FR-18.3/18.4, L17b and FR-7.32 added; FR-7.15 superseded by FR-7.31; R-13 and a host stall row for FR-15.11; FR-9.4 contract reachable under §5.3; FR-15.12/15.13/NFR-19.1 contract fixes
  - 0.3.6 2026-10-23 carried Load-defined observations: §5.3 Prohibited line scoped to stored credentials; no-network build carries a distinct version; FR-15.12/FR-15.13 contracts state the download-duration need instead of a 3 MB size
  - 0.3.7 2026-10-23 Load-defined findings: R-13 no longer claims a host stall core (stall clause blocked on bench #55); hang build logs its anchor line and FR-19.1/NFR-19.1 timing states the bench clock resolution; NFR-5.2/NFR-5.3 tolerances aligned; FR-5.21 scans with the AP stopped; FR-5.28 resets through JTAG with the AP down; Phase 2 exit names bench #54; Appendix B parses as YAML
  - 0.3.8 2026-10-24 verification waivers accepted by the user for shipment (Appendix B); R-14 (bench #54), R-15 (one bench radio), R-16 (UDP listener) added; bench clock resolution in Appendix A; R-12/R-13 order
  - 0.3.9 2026-10-24 waiver table honest: NFR-5.1, FR-16.3, FR-17.1 fully waived (no test evidence); FR-19.1 early-restart prohibition stated with the clock resolution
  - 0.4.0 2026-10-29 cloned for the T-Display-S3 AMOLED 1.91": §2.2 and C-6.1 – C-6.5 restated for the RM67162 over QSPI (pins, GPIO 38 power, MADCTL 0x60), FR-6.4/FR-6.9/FR-6.10 contracts on 536 × 240, FR-6.7 lights the panel by its brightness register; bench SLOT1; C-6.4 75 MHz set by the user
  - 0.5.0 2026-10-30 §21 Deep Sleep (FR-21.1 – FR-21.12, NFR-21.1) from the user's answers and weather-station example, approved by the user; L18, L19
  - 0.6.0 2026-10-30 §22 Time (FR-22.1 – FR-22.5, NFR-22.1) from the user's request; L20, R-19; server, zone and 10 s deadline approved by the user
  - 0.7.0 2026-10-30 §23 Weather Sensor (FR-23.1 – FR-23.10, NFR-23.1, NFR-23.2, C-23.1) from the user's answers (BME280 0x76 on GPIO 43/44, forced x1, all outputs, 600 s); FR-6.9 superseded by FR-6.11 (four screen lines); L21, L22, R-20, R-21; approved by the user
  - 0.7.1 2026-10-30 FR-22.2 time zone set to US Eastern (EST5EDT) by the user
  - 0.7.2 2026-10-30 readings shown in °F (screen, page, L21, `temp_f` in /api/readings) by the user
  - 0.8.0 2026-10-30 from the user's request, approved by the user (humidity a whole number on line 4; 5 s live reading, 50 ms debounce): §24 Setup Check (FR-24.1 – FR-24.8, L23, L24, R-22, Phase 6); five 33-character lines (FR-6.10, FR-6.11 superseded by FR-6.13, FR-6.12); `Hum` reading line (FR-23.6 superseded by FR-23.11); countdown logging (FR-7.11 superseded by FR-7.33)
  - 0.8.1 2026-10-30 FR-22.6 from the user's request: a reading from a never-set clock shows no time (page, `null` in /api/readings, `at=none` in L21)
  - 0.9.0 2026-10-30 pressure in mbar instead of hPa, approved by the user: FR-23.11 superseded by FR-23.12 (line 4), FR-23.8 superseded by FR-23.13 (`p_mbar` in /api/readings); page, L21, §23.1, §23.2, contracts and R-21 say mbar; the LED next to the user key flashes at 1 Hz during the setup check (FR-24.9), and line 5 says so (FR-24.4 superseded by FR-24.10)
  - 0.10.0 2026-10-30 §25 Summary Email (FR-25.1 – FR-25.10, NFR-25.1) from the user's answers (direct SMTP to Gmail or own server on 465/587 with a public certificate, settings in the portal, one recipient, plain text, summary at a set time and interval, a failed send dropped and logged, WiFi on a timer wake only for a due summary); FR-21.10 superseded by FR-21.13; L25, L26, R-23, Phase 7; §4.3, §9.1 and Appendix A updated
  - 0.11.0 2026-10-30 the BOOT key, next to the blinking LED, ends the setup check as well as the user key, approved by the user: FR-24.5 superseded by FR-24.11, FR-24.10 superseded by FR-24.12 (`Press BOOT to start <mm>:<ss>`), FR-18.2 superseded by FR-18.5; FR-24.9 names the LED's place by the BOOT key; FR-25.11 (a send cut short by a reset is tried once more)
superseded_requirements: [FR-5.1, FR-5.2, FR-5.3, FR-5.4, FR-5.5, FR-5.6, FR-5.7, FR-5.8, FR-5.9, FR-5.10, FR-7.1, FR-7.2, FR-7.3, FR-7.4, FR-9.1, C-10.4, FR-15.2, FR-15.3, FR-15.4, FR-15.6, FR-15.7, FR-14.1, FR-14.2, FR-5.22, FR-6.5, FR-6.6, FR-15.5, FR-18.1, FR-7.15, FR-6.9, FR-6.10, FR-6.11, FR-7.11, FR-23.6, FR-23.11, FR-23.8, FR-24.4, FR-21.10, FR-24.5, FR-24.10, FR-18.2]
open_decisions: []
related_test_baseline: none yet
```
