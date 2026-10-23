# T-Display-S3 Webserver — Functional Specification Document (FSD)

This document is the **WHAT** plane: what must be true of the firmware. The plane
map is [`../00-Overview.md`](../00-Overview.md); the other planes are
[`../Method/`](../Method/00-Overview.md) (HOW) and the
[User Manual](../UserDocumentation/User-Manual.md) (OPERATE).

**Status: not yet defined.** This project starts from the `tdisplay-s3-hello`
base: board bring-up, LCD driver, serial log, CI and bench scaffolding. No
webserver requirement exists yet; `/define` writes them. Until then, nothing in
this document is approved, and no test can verify anything against it.

**Conventions.** Requirements are `FR-x.y` (functional), `NFR-x.y` (quality) and
`C-x.y` (constraint), each with a priority `[Must/Should/May]` and a provenance tag:
`[user]` stated by the user; `[derived]` a logical consequence of a `[user]`
item or a board fact; `[proposed]` a value this document proposes that has not yet
been accepted (listed under *Open decisions*, Appendix B). Everything not tagged
`[proposed]` is approved.

## Contents

| § | Section |
|---|---------|
| 1 | System Overview |
| 2 | Hardware / Platform |
| B | Lifecycle Metadata |

---

## 1. System Overview

Not yet defined.

## 2. Hardware / Platform

The board facts below hold for the unit in SLOT4. They are carried from
`tdisplay-s3-hello` as `[proposed]` until `/define` accepts them for this project.

| Item | Value | Source |
|------|-------|--------|
| Board | LilyGO T-Display-S3, non-touch variant | [proposed] |
| MCU | ESP32-S3R8: 16 MB flash, 8 MB octal PSRAM | [derived from the board] |
| Display | ST7789 controller, 170×320 visible pixels, RGB565, 8-bit Intel-8080 (i80) parallel bus | [derived from the board] |
| LCD data pins | D0 – D7 = GPIO 39, 40, 41, 42, 45, 46, 47, 48 | [derived from the LilyGO T-Display-S3 pin map] |
| LCD control pins | WR = GPIO 8, RD = GPIO 9, DC = GPIO 7, CS = GPIO 6, RST = GPIO 5 | [derived from the LilyGO T-Display-S3 pin map] |
| LCD power enable | GPIO 15 (active high) | [derived from the LilyGO T-Display-S3 pin map] |
| Backlight | GPIO 38 (active high) | [derived from the LilyGO T-Display-S3 pin map] |
| Visible-area offset | The 170-pixel axis starts at controller address 35 (the ST7789's 240-line RAM, centred) | [derived from the board] |
| Power and console | USB-C on the board, which is the ESP32-S3's native USB-Serial/JTAG | [derived from the board] |
| Bench slot | SLOT4 (EN/BOOT GPIOs **not wired**; reset over USB only) | [derived from the testbench device list, 2026-10-20] |

---

## Appendix B — Lifecycle Metadata

```yaml
document_status: stub
fsd_version: 0.0.0
repository: https://github.com/solchjg/tdisplay-s3-webserver
baseline_commit: (initial commit of this document)
applicable_firmware_version: none yet
author: solchjg
reviewers: []
approval_status: nothing approved
created: 2026-10-23
last_updated: 2026-10-23
change_history:
  - 0.0.0 2026-10-23 stub, project created from the tdisplay-s3-hello base
superseded_requirements: []
open_decisions:
  - Every requirement: the webserver's function, network role (SoftAP / station), display content, security profile
  - Whether the tdisplay-s3-hello build constraints (esp32s3, ESP-IDF v6.0.2, 16 MB flash, single-app partition, USB-only flash/reset) carry over; a webserver with OTA needs a different partition table
related_test_baseline: none yet
```
