# T-Display-S3 Webserver — Functional Specification Document (FSD)

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
| 11 | Operational Procedures | operations |
| 12 | Verification & Validation | operations |
| A | Constants & Configuration Catalogue | |
| B | Lifecycle Metadata (incl. superseded requirements, open decisions) | |

---

## 1. System Overview

Firmware for the LilyGO T-Display-S3 (ESP32-S3 with a built-in 1.9-inch ST7789
170×320 LCD) that joins an existing WiFi network as a station and serves a status
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
PSRAM use, backlight dimming.

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
                    │  WiFi/IP events ─► Device Logic (§5) ─► LCD Display Interface (§6) ──────┼─ i80 ─► ST7789
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
| Board | LilyGO T-Display-S3, non-touch variant | [user] |
| MCU | ESP32-S3R8: 16 MB flash, 8 MB octal PSRAM (PSRAM unused) | [derived from the board] |
| Display | ST7789 controller, 170×320 visible pixels, RGB565, 8-bit Intel-8080 (i80) parallel bus | [derived from the board] |
| LCD data pins | D0 – D7 = GPIO 39, 40, 41, 42, 45, 46, 47, 48 | [derived from the LilyGO T-Display-S3 pin map] |
| LCD control pins | WR = GPIO 8, RD = GPIO 9, DC = GPIO 7, CS = GPIO 6, RST = GPIO 5 | [derived from the LilyGO T-Display-S3 pin map] |
| LCD power enable | GPIO 15 (active high) | [derived from the LilyGO T-Display-S3 pin map] |
| Backlight | GPIO 38 (active high) | [derived from the LilyGO T-Display-S3 pin map] |
| i80 pixel clock | 10 MHz | [user] |
| Visible-area offset | The 170-pixel axis starts at controller address 35 (the ST7789's 240-line RAM, centred) | [derived from the board] |
| BOOT button | GPIO 0, active low, external pull-up on the board | [derived from the LilyGO T-Display-S3 pin map] |
| Radio | ESP32-S3 2.4 GHz WiFi (802.11 b/g/n); BLE present but unused (FR-9.2) | [derived from the board] |
| Power and console | USB-C on the board, which is the ESP32-S3's native USB-Serial/JTAG | [derived from the board] |
| Flash layout | Two 4 MB application slots, see C-10.6 | [user] |
| Bench slot | SLOT4 (EN/BOOT GPIOs **not wired**; reset over USB only) | [derived from the testbench device list, 2026-10-20] |

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
│                       │ mDNS Responder (§17) · BOOT Button (§18)         │
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
  state, the retry schedule, and the confirm-or-roll-back decision. Pure
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
  FR-7.5 – FR-7.7, FR-7.11 – FR-7.13, FR-7.29, FR-7.30 (image check); FR-19.1,
  NFR-19.1 (variant app); FR-9.2; the LCD fault build and the hang build
  (variant `app`) of §12.0.
- **Exit criteria.** On SLOT4, after a USB flash of the CI artefact and a USB
  reset: the boot line of FR-7.7 and `Init complete, running event-driven` appear;
  the LCD shows `Starting` (operator); `idf.py size` fits C-10.7; the
  watchdog hang build resets with reason `TASK_WDT`.
- **Dependencies.** Testbench USB flash, serial monitor and operator prompt; the
  commissioned panel (`lcd-st7789`). Nothing from a later phase.

### 3.2 Phase 2: Provisioning and connectivity

- **Scope.** §5 connectivity state machine, the portal (§13), credential storage,
  reconnect with backoff, the screen content of every state.
- **Deliverables.** FR-5.11 – FR-5.20, FR-5.25, FR-5.30, NFR-5.1 – NFR-5.3; §13;
  FR-7.8 – FR-7.10, FR-7.17 – FR-7.24, FR-7.30 (wire); FR-9.3 (serial part), FR-9.4.
- **Exit criteria.** From an erased NVS, the testbench `enter-portal` procedure
  provisions the device through `POST /connect`; the device joins the bench AP and
  logs `web: online`; after a USB reset it rejoins without the portal; after the
  bench AP is stopped and restarted it rejoins within NFR-5.2.
- **Dependencies.** Phase 1 (serial log is the evidence channel); testbench WiFi
  AP, station join, HTTP relay and WiFi scan.

### 3.3 Phase 3: Status server

- **Scope.** Status page, JSON API, request counter, UDP log, mDNS, identity,
  HTTP-handler watchdog, display failure under network load.
- **Deliverables.** §14, §16, §17, §20, FR-5.26, FR-5.28, FR-5.31 (bench), FR-7.14, FR-7.26, FR-9.3 (UDP and
  HTTP parts), FR-9.5, FR-19.2, NFR-19.1 (variant http); the hang build variant `http`.
- **Exit criteria.** Through the bench HTTP relay: `GET /` and `GET /api/status`
  answer per §14 with consistent values; the LCD request count follows (FR-5.25,
  operator plus FR-7.11 mirror); NFR-14.1 holds; only TCP port 80 is open.
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

---

## 4. Risks, Assumptions & Dependencies

### 4.1 Risks

| ID | Risk | Likelihood | Impact | Mitigation |
|----|------|-----------|--------|------------|
| R-1 | The serial mirror proves what the firmware *intended* to draw, not what the panel shows | Certain | Medium | Display content is confirmed by an operator (FR-5.25, FR-6.1, FR-6.3, FR-6.4); FR-7.11 makes the intent automatable |
| R-2 | The i80 bus is write-only, so a real panel fault is invisible; the display-failure path can be exercised only by injecting a driver error | Certain | Low | Verified on the LCD fault-injection test build (§12.0) |
| R-3 | Boot deadlines are read from the device's own log timestamp, which counts from chip reset, not from the bench's reset command | Certain | Low | Accepted: the device clock is the only clock that sees both the reset and the line |
| R-4 | Wrong mirror, colour-inversion or offset settings produce an image every serial-only test misses | Medium | Medium | Operator checks FR-6.1, FR-6.3, FR-6.4 each name the defect to look for |
| R-5 | SLOT4 is shared with `esp32-oled-hello` | Certain | Low | Every bench test first confirms the chip and MAC recorded in `testing/test-plan.yaml` |
| R-6 | The device downloads images from the bench's firmware route on port 8080; reachability from the bench AP network is the bench's declaration, not yet exercised by this project | Medium | Medium | Depended on as declared; if FR-15.10's first bench run disproves it, the fault is filed against the testbench, never worked around here |
| R-7 | The captive-DNS check (FR-13.5) assumes the bench's HTTP relay resolves names through the DUT's SoftAP DNS | Medium | Low | FR-13.5 is a Should; the bench does not declare this ability, so its bench test is blocked until it does (testbench change request, never a project workaround) |
| R-8 | The mDNS check (FR-17.1) assumes the bench Pi resolves `.local` names on its AP network | Medium | Low | FR-17.1 is a Should; the bench does not declare this ability, so its bench test is blocked until it does (testbench change request, never a project workaround) |
| R-9 | BOOT (GPIO 0) is not wired on SLOT4: factory reset can only be tested by an operator; holding BOOT while the chip resets enters the ROM download mode instead | Certain | Low | FR-18.x contracts are operator-tier and press only while the device is running |
| R-10 | Credentials that can never connect (wrong password, AP gone for good) leave the device retrying forever, because it never returns to the portal on its own (FR-5.19) | Certain | Medium | Accepted [user]: the BOOT-button factory reset (§18) is the recovery path |
| R-11 | The bench AP and the device SoftAP both use 192.168.4.1 | Certain | Low | The bench's provisioning procedure leaves the device SoftAP before it starts its own AP; the device's SoftAP is gone in every state but PROVISIONING (FR-9.4) |
| R-13 | No peer on the bench AP network can stall a download (the bench declares no such mode), so the stall clause of FR-15.11 has no bench run | Certain | Medium | Its logic is verified at host tier against a fake clock (FR-15.11 stall core row); the bench run is blocked until the testbench declares a stall mode (change request drafted) |
| R-12 | The bench WiFi scan reports no BSSID | Certain | Low | SoftAP presence is matched by the device's own SSID (FR-13.1), which the device logs (FR-7.9) |

### 4.2 Assumptions

- The board is powered from USB-C; no battery is fitted (assumed; not
  architecture-critical).
- The LAN the device joins is trusted (§9.1). [user]

### 4.3 Explicitly out of scope

- Authentication on any endpoint; TLS; signed firmware, secure boot, flash
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
- Embedded AI Harness testbench at `192.168.2.230`, SLOT4: USB flash, serial
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
| Screen lines 1 – 3 | derived from state | ASCII, ≤ 20 characters each | FR-5.29 | §5.4 |

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
- **Prohibited:** any transition into PROVISIONING other than T1 after T10's
  erase (FR-5.19); any restart other than T10, T11, T13 and the watchdog.

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
| D4 | LCD_OK | 1 s tick | changed, write ok | LCD_OK | Redraw each changed line; log FR-7.11 for each | 1000 ± 100 ms period |
| D5 | LCD_OK | 1 s tick | changed, write failed | LCD_FAILED | Log `web: lcd write failed: <ERR>` | same tick |
| D6 | LCD_FAILED | 1 s tick | — | LCD_FAILED | No LCD call | — |

Completeness: LCD_START × tick: impossible, the tick starts on exit from
LCD_START. LCD_OK × start done and LCD_FAILED × start done: impossible — the LCD is
started exactly once, in LCD_START. Connectivity events never change the display
region; they change the screen content that D3 – D5 compare against.

### 5.4 Screen content

Three text lines, landscape, at most 20 characters each.

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
- **No-network build** — a valid image identical to the shipped one except that it
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
| FR-5.25 | Each connectivity state reached on the bench; operator prompted per state | Operator confirms the §5.4 lines; the FR-7.11 mirror lines match them | Missing, stale or extra text | n/a | bench (operator) |
| FR-5.25 (core) | Screen function for every state, with sample SSID/IP/n/hostname | The §5.4 strings exactly | Any other string | n/a | host |
| FR-5.26 | ONLINE; one `GET /api/status` | FR-7.11 line `line2="Req: <n>"` with the new n | No mirror line | ≤ 2 s after the response | bench |
| FR-5.27 | LCD fault build failing at start; USB reset; 30 s capture | Exactly one `lcdfi: call` before `web: lcd init failed:` | Any `lcdfi: call` after it | 30 s window | target |
| FR-5.31 (core) | Screen function for every §5.3 state and every display-region state | No line equals `Hello, world!`; no line starts `Up:` | Either text in any state | n/a | host |
| FR-5.31 | Shipped build, JRN-01 run from erase to ONLINE with 3 requests; operator prompted at PROVISIONING and ONLINE | No FR-7.11 mirror line with either text; operator confirms neither is on the panel | `Hello, world!` or `Up:` on the panel or in a mirror line | n/a | bench (operator) |
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
    - "web: state=WIFI_LOST" within 15 s of the AP stopping (NFR-5.3).
    - "web: retry in" lines during the outage.
    - "web: online ip=" after the AP restarts.
  timing: "web: online" within 30 s of the AP start, by bench wall clock of the UDP or serial capture
  tolerance: none beyond the deadline
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
    - USB reset; wait for ONLINE. Then an operator unplugs and replugs SLOT4's USB cable (power cycle).
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
    - The FR-13.1 SSID appears in a bench scan.
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
    - USB reset; after the LCD failure line, request GET /api/status every 5 s for 60 s.
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

Powers the built-in ST7789 panel, turns on its backlight, renders up to three text
lines in landscape, and reports whether each driver call succeeded. Direction:
device → display.

### 6.2 Protocol

ESP-IDF esp_lcd i80 bus, 8 data lines, at the pixel clock of C-6.4, with the pins
of C-6.2. RD is held high. "Display failed" means an esp_lcd call (bus creation,
panel initialisation, or bitmap draw) returns a value other than `ESP_OK`. The bus
has no acknowledge, so a physically absent or broken panel is not detected (R-2).

### 6.3 Requirements

- **C-6.1** [Must] [derived from the LilyGO T-Display-S3 pin map] The i80 data
  lines D0 – D7 shall be GPIO 39, 40, 41, 42, 45, 46, 47, 48 in that order.
- **C-6.2** [Must] [derived from the LilyGO T-Display-S3 pin map] The LCD control
  lines shall be WR = GPIO 8, DC = GPIO 7, CS = GPIO 6, RST = GPIO 5, and
  RD = GPIO 9 driven high.
- **C-6.3** [Must] [derived from the LilyGO T-Display-S3 pin map] GPIO 15 (LCD
  power enable) shall be driven high before panel initialisation begins.
- **C-6.4** [Must] [user] The i80 pixel clock shall be 10 MHz.
- **C-6.5** [Must] [derived from the board] The panel's 170-pixel axis shall be
  addressed with an offset of 35 controller lines.
- **FR-6.1** [Must] [user] In LCD_OK, text shall be upright and read left to right
  when the board is held in landscape with its USB-C connector to the viewer's
  right.
- **FR-6.2** [Must] [user] Text shall be rendered in a fixed-width font with a
  16 × 32 pixel character cell covering ASCII 0x20 – 0x7E.
- **FR-6.3** [Must] [user] In LCD_OK, text shall be white on a black background.
- **FR-6.4** [Must] [derived from FR-5.25] On entry to LCD_OK (D1), the full
  320 × 170 visible area shall be cleared to black, leaving no lit pixels outside
  the text lines.
- **FR-6.9** [Must] [derived from §5.4] Lines 1, 2 and 3 shall not overlap and
  shall lie entirely inside the visible area.
- **FR-6.10** [Must] [derived from §5.4 and FR-6.2] A line of 20 characters shall
  be displayed in full, without clipping or wrapping (20 × 16 px = 320 px).
- **FR-6.7** [Must] [derived from FR-5.25] In LCD_OK, GPIO 38 (backlight) shall be
  driven high.
- **FR-6.8** [Must] [derived from FR-5.26] Redrawing a line shall overwrite its
  whole row, leaving no characters of its previous, longer text.

### 6.4 Verification contracts

| ID | Precondition · stimulus | Expected observation | Must NOT happen | Tier |
|----|------------------------|----------------------|-----------------|------|
| C-6.1 – C-6.5 | Inspect the built configuration | The pin numbers, 10 MHz clock and offset 35 appear in the firmware's own configuration | Any other value; a value taken from a driver default | host |
| C-6.1 – C-6.5 (wire) | Shipped build; reset; operator prompted after the BOOT screen | Operator confirms `Starting` is legible | Blank, noisy, shifted or garbled panel | bench (operator) |
| FR-6.1 | LCD_OK; operator prompted with the board held USB-C to the right | Operator confirms text upright and left to right | Mirrored, upside-down or rotated text | bench (operator) |
| FR-6.2 | Inspect the font used by the renderer | Cell 16 × 32 px; glyphs 0x20 – 0x7E | Other cell size; proportional font | host |
| FR-6.3 | LCD_OK; operator prompted | Operator confirms white on black | Inverted or tinted colours | bench (operator) |
| FR-6.4 | Power-cycle (random panel RAM); operator prompted after the first screen | No stray pixels along any edge or between lines | Noise band along an edge; remnants | bench (operator) |
| FR-6.9 | Render three lines with the shipped layout | Each line's top row > the previous line's bottom row; all glyphs inside x ∈ [0, 319], y ∈ [0, 169] | Overlap; a pixel outside | host |
| FR-6.10 | Render a 20-character line | Rightmost pixel x ≤ 319; one row | Width > 320 px; wrap | host |
| FR-6.7 | LCD_OK; operator prompted | Backlight on | Off or flickering | bench (operator) |
| FR-6.8 | Render `Req: 1000`, then `Req: 5` into the same row | Row after the second render equals a fresh render of `Req: 5` | Remnant pixels of `000` | host |

### 6.5 Failure modes

Any esp_lcd call that does not return `ESP_OK` → the interface reports "display
failed" with the `esp_err_t` value to §5; it never retries on its own and never
aborts. What happens next is §5's decision (D2, D5).

---

## 7. Serial Log Interface

### 7.1 Purpose & peer

The machine-readable record of every decision, read by the testbench serial
monitor on SLOT4 and, in ONLINE, mirrored over UDP (§16). Direction: device →
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
- **FR-7.11** [Must] [derived from R-1] Each line redrawn by D4 shall log L7 with
  the exact text drawn.
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
| FR-7.11 | ONLINE; one request | L7 `line2="Req: <n>"` | Text differing from the drawn line | bench |
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
| FR-15.11 (stall, core) | Download state core with a fake clock: bytes arrive, then none for 29.9 s; then none for 30.0 s | No failure at 29.9 s; failure (K11 action) at 30.0 s | Failure before 30 s; no failure at 30 s | host |
| FR-15.12 | ONLINE running build A; host build A's own 3 MB-or-larger image; request it; request again within 1 s | Second answer 409; first download completes and re-installs fw=A (T11) | Two downloads; the second answered 202 | bench |
| FR-15.12 (core) | Download state machine: second start while busy | Refused with 409 | Accepted | host |
| FR-15.13 | ONLINE running build A; host a 3 MB build B; request it; stop the bench AP 2 s after the bench log shows the fetch starting; restart the AP after 20 s | K11 and `web: state=WIFI_LOST`; after the AP returns, `web: online`, `/api/status` reports `fw_version` = A, and a following USB reset boots `fw=A` on the same `slot=` | K10; a restart into B; `OTA succeeded` after the disconnect | bench |
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

The on-board BOOT button (GPIO 0, active low) is the only local control; a long
press requests a factory reset, which happens on release, so the button is never
held while the chip resets (R-9). Sampled only after BOOT exits.

### 18.2 Requirements

- **FR-18.3** [Must] [user] Releasing the button after holding it continuously
  for at least 5 s shall raise exactly one factory-reset event (§5.3 T10), at the
  release.
- **FR-18.4** [Must] [derived from FR-18.3] While the button is held, the device
  shall raise no factory-reset event and shall not restart.
- **FR-18.2** [Must] [derived] Releasing the button before 5 s shall
  raise no event and change nothing.

### 18.3 Verification contracts

| ID | Precondition · stimulus | Expected observation | Must NOT happen | Tier |
|----|------------------------|----------------------|-----------------|------|
| FR-18.3 | See FR-5.21 (full form) | L17b, then L15 and a boot line after release | A reset while held (ROM download-mode banner, no boot line) | bench (operator) |
| FR-18.3 (core) | Hold detector fed samples: low 4.9 s then high; low 5.0 s then high; low 12 s then high; low 30 s with no release | No event; one event at the release; one event at the release; no event | An event while still low; an event under 5 s; two events for one hold | host |
| FR-18.4 | ONLINE; operator holds BOOT for ~12 s, then releases | L17b about 5 s into the hold; no L15 and no boot line before the release (operator confirms the release time) | L15 or a restart while held | bench (operator) |
| FR-18.2 | ONLINE; operator presses BOOT for ~2 s | No L15, no boot line, still ONLINE 10 s later | Factory reset; restart | bench (operator) |

---

# Part C — Foundation (L0)

## 8. ESP-IDF Platform

### 8.1 Purpose

ESP-IDF v6.0.2 supplies the WiFi driver (SoftAP and station), esp_netif/lwIP with
DHCP server and client, esp_http_server, nvs_flash, app_update and the
rollback-capable bootloader, esp_task_wdt, esp_lcd with the ST7789 driver, the GPIO
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
| Remote exposure | HTTP on port 80 (status, OTA request) in ONLINE; outbound HTTP to whatever URL an OTA request names; open SoftAP with portal in PROVISIONING; mDNS; outbound UDP logs |
| Confidentiality | **Not claimed.** HTTP and UDP logs are plaintext; NVS is not encrypted, so the WiFi password is readable from flash |
| Integrity / authenticity of firmware | **Not claimed.** Anyone on the LAN can upload an image; no signature check; secure boot off |
| Credentials | One WiFi password, stored in NVS; never output by the device (FR-9.3) |

Accepted risks [user]: unauthenticated OTA (anyone on the LAN can make the device
download and run any image from any HTTP URL); open portal SoftAP (anyone in radio
range during PROVISIONING can submit credentials or read submitted ones);
plaintext HTTP and logs; unencrypted NVS. The profile justifies these protections
only: never disclosing the password, keeping the radio surface to what the
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
- **C-10.5** [Must] [derived from SLOT4's unwired EN/BOOT] The firmware shall be
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
| C-10.3 (board) | Read the flash ID in SLOT4 | 16 MB | Smaller | bench |
| C-10.5 | Flash and reset SLOT4 using only USB | Flash succeeds; L1 and L17 appear | Need for EN/BOOT or a button | bench |
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
HTTP handler on `GET /hang` (variant `http`).

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
  timing: TASK_WDT boot 10 s – 15 s after the hang starts (NFR-19.1; by the last pre-hang log timestamp plus the bench capture clock)
  tolerance: + 3 s late (Appendix A), never early
  prohibited_outcomes:
    - A restart before 10 s (timeout too short).
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
| NFR-19.1 | As FR-19.1 (target) and FR-19.2 (bench) | Restart 10 s – 15 s after the hang | Earlier than 10 s; later than 15 s + 3 s | target, bench |
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
| **target** | T-Display-S3 in SLOT4, no network peer; shipped or test build | Log formats and order, LCD failure path (LCD fault build), watchdog (hang build) |
| **bench** | T-Display-S3 + testbench WiFi AP/station, HTTP relay, UDP log, scans, operator | Provisioning, connectivity and recovery, HTTP API, OTA and rollback (no-network build), mDNS, security scans, display content, button |

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

Build-time constants:

| Name | Value | Source |
|------|-------|--------|
| LCD pins, clock, offset | §2.2, C-6.1 – C-6.5 | [user]/[derived] |
| Landscape area / font cell / colours | 320 × 170 / 16 × 32 / `0xFFFF` on `0x0000` | [user] |
| Screen lines / max characters | 3 / 20 | [derived] |
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
| ONLINE: AP loss to WIFI_LOST | ≤ 15 s | [user] |
| `POST /ota` answer | ≤ 2 s | [user] |
| Failed (non-stalled) OTA download to K11 | ≤ 60 s | [user] |
| Timing tolerance of restart and detection deadlines | up to + 3 s late, never early | [user] |
| UDP log destination | station gateway, port 5555 | [user] |
| Log tag | `web` | [derived] |

## Appendix B — Lifecycle Metadata

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
| FR-6.6 | The 16-character uptime line fits one row | FR-6.10 (any 20-character line) |
| FR-18.1 | Factory-reset event after 5 s of holding (restart while still held) | FR-18.3, FR-18.4 (event at release) |
| FR-7.15 | OTA outcome lines L11/L14 in their 0.1.0 meaning | FR-7.31 |
| FR-15.5 | 409 while an upload is in progress | FR-15.12 (while a download is in progress) |
| FR-14.1, FR-14.2 | Status page and JSON including uptime (`uptime_s`, 7 keys) | FR-14.7, FR-14.8 (6 keys, no uptime) |
| FR-15.2 – FR-15.4, FR-15.6, FR-15.7 | OTA by pushed image body (411, 413, 400, 200 answers; broken upload) | FR-15.8 – FR-15.11 (OTA by URL pull, matching the testbench's OTA flow) |

FR-6.1 – FR-6.4 and FR-6.7 keep their IDs and meaning; only the name of the state
they apply in changed (SHOWING → LCD_OK, §5.3's display region).

```yaml
document_status: draft
fsd_version: 0.3.5
repository: https://github.com/solchjg/tdisplay-s3-webserver
baseline_commit: (the commit introducing this version)
applicable_firmware_version: none yet
author: solchjg
reviewers: []
approval_status: all requirements and values approved by the user
created: 2026-10-23
last_updated: 2026-10-23
change_history:
  - 0.0.0 2026-10-23 stub, project created from the tdisplay-s3-hello base
  - 0.1.0 2026-10-23 initial definition: station + portal, status page/API, HTTP OTA with rollback, watchdog, factory reset, UDP log, mDNS
  - 0.1.1 2026-10-23 all proposed values accepted by the user (Appendix A)
  - 0.2.0 2026-10-23 OTA by URL pull (testbench OTA flow), FR-15.2-15.4/15.6/15.7 superseded by FR-15.8-15.11, FR-5.22 and T11/T12 restated; testbench compatibility log lines K1-K12 (FR-7.18-7.29); R-6 restated
  - 0.2.1 2026-10-23 OTA URL limit (256 characters) and download stall timeout (30 s) accepted by the user
  - 0.3.0 2026-10-23 hello world and uptime removed: FR-14.1/14.2 superseded by FR-14.7/14.8 (no uptime_s); FR-5.31 (no Hello, world! or Up: on screen) and FR-7.30 (no hello log tag) added
  - 0.3.1 2026-10-23 Load-defined findings: FR-5.22, FR-6.5, FR-6.6, FR-15.5 superseded by FR-5.32, FR-6.9, FR-6.10, FR-15.12 (meaning had changed); completeness lists made explicit; NFR-5.3, NFR-15.1, NFR-15.2, NFR-19.1 and the + 3 s tolerance state numbers contracts used; FR-5.24 requires L16; FR-17.2 observed via bench WiFi events; R-6 restated
  - 0.3.2 2026-10-23 CONNECTING × disconnected listed (counts as AF, T6); Appendix A restart row names T13
  - 0.3.3 2026-10-23 FR-5.24 test build is a no-network build (reaching ONLINE confirms, by T5; user decision); FR-7.25 moved to Phase 4; FR-5.15 contract needs no line order; FR-15.11 stall prohibits late K11; FR-9.2 air MAC range stated; R-7/R-8 defer to the bench declaration
  - 0.3.4 2026-10-23 FR-15.13 (T7 aborts an OTA download; derived from §15.1) closes OS/OF outside ONLINE; L14 logged on every boot while a slot is recorded invalid, contracts check slot= instead of L14's absence; FR-15.11 stall follow-up re-installs A; FR-19.1 evidence fits target tier
  - 0.3.5 2026-10-23 factory reset on release (user decision): FR-18.1 superseded by FR-18.3/18.4, L17b and FR-7.32 added; FR-7.15 superseded by FR-7.31; R-13 and a host stall row for FR-15.11; FR-9.4 contract reachable under §5.3; FR-15.12/15.13/NFR-19.1 contract fixes
superseded_requirements: [FR-5.1, FR-5.2, FR-5.3, FR-5.4, FR-5.5, FR-5.6, FR-5.7, FR-5.8, FR-5.9, FR-5.10, FR-7.1, FR-7.2, FR-7.3, FR-7.4, FR-9.1, C-10.4, FR-15.2, FR-15.3, FR-15.4, FR-15.6, FR-15.7, FR-14.1, FR-14.2, FR-5.22, FR-6.5, FR-6.6, FR-15.5, FR-18.1, FR-7.15]
open_decisions: []
related_test_baseline: none yet
```
